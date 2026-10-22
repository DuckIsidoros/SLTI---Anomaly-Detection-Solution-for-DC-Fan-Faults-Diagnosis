#include <Arduino.h>
#include <Wire.h>
#include <string.h>

#include "dsp_edge.h"
#include "window_manager.h"
#include "model_runner.h"
#include "mqtt_manager.h"

// Configuration for MPU6050:

#define I2C_SDA 21
#define I2C_SCL 22

#define IMU_ADDRESS 0x68
#define IMU_REG_PWR_MGMT_1 0x6B
#define IMU_REG_SMPLRT_DIV 0x19
#define IMU_REG_CONFIG 0x1A
#define IMU_REG_ACCEL_CONFIG 0x1C
#define IMU_REG_ACCEL_XOUT_H 0x3B

// +- 16g
static const float ACCEL_SCALE = 1.0f / 2048.0f;

namespace
{
    // FreeRTOS task handling & inter-core synchronization primitives
    TaskHandle_t inferenceTaskHandle = nullptr;
    portMUX_TYPE windowMux = portMUX_INITIALIZER_UNLOCKED;

    constexpr uint32_t SAMPLE_PERIOD_US = 2000;

    // Thread-safe snapshot buffer for DSP execution on core 1 (reused in-place for FFT magnitudes)
    float windowSnapshot[AXIS_COUNT][WINDOW_SIZE];

    // Notification throttle
    volatile bool windowPending = false;

    float modelInput[MODEL_INPUT_SIZE];
    float conditionVector[2];

    // User-configurable deployment environment: set to ENV_TABLE or ENV_FLOOR
    Environment currentEnv = ENV_TABLE;

    inline const char *getEnvName(Environment env)
    {
        return (env == ENV_TABLE) ? "ENV_TABLE" : "ENV_FLOOR";
    }

    void applyEnvironmentConfig(Environment env)
    {
        currentEnv = env;
        setActiveEnvironment(env);
        prepareConditionVector(env, conditionVector);
    }

    // Alarm latch settings
    constexpr uint32_t ALARM_LATCH_MS = 3000;

    // We have developed an algorithm to trigger anomalies if and only if 10 windows / 20 windows are anomalies.
    // You can find further explanation in the document.
    constexpr uint32_t ANOMALY_WINDOW_SPAN = 20;
    constexpr uint32_t ANOMALY_COUNT_THRESHOLD = 10;

    uint8_t anomalyRing[ANOMALY_WINDOW_SPAN];
    uint32_t anomalyRingIndex = 0;
    uint32_t anomalyCount = 0;

    uint32_t lastAnomalyTimestamp = 0;
    bool alarmActive = false;

    // To reset the saved states if anomalies (caused by users' iteraction rather than real fauls) end.
    constexpr uint32_t EPISODE_RESET_NORMAL = 3;
    uint32_t faultStartTimestampMs = 0;
    uint32_t consecutiveNormalWindows = 0;
    bool episodeActive = false;

    // I2C failure handling
    constexpr uint32_t I2C_FAILURE_THRESHOLD = 10;
    volatile uint32_t consecutiveI2CFailures = 0;
    volatile bool sensorFaultLatched = false;
    bool sensorFaultAlertSent = false;
    float peakMse = 0.0f;
    uint32_t lastHeartbeatTime = 0;

    bool initMpu6050(void)
    {
        Wire.begin(I2C_SDA, I2C_SCL);
        Wire.setClock(100000);

        Wire.beginTransmission(IMU_ADDRESS);
        Wire.write(IMU_REG_PWR_MGMT_1);
        Wire.write(0x00);
        if (Wire.endTransmission() != 0)
        {
            return false;
        }

        Wire.beginTransmission(IMU_ADDRESS);
        Wire.write(IMU_REG_CONFIG);
        Wire.write(0x03);
        if (Wire.endTransmission() != 0)
        {
            return false;
        }

        Wire.beginTransmission(IMU_ADDRESS);
        Wire.write(IMU_REG_SMPLRT_DIV);
        Wire.write(0x01);
        if (Wire.endTransmission() != 0)
        {
            return false;
        }

        Wire.beginTransmission(IMU_ADDRESS);
        Wire.write(IMU_REG_ACCEL_CONFIG);
        Wire.write(0x18);
        if (Wire.endTransmission() != 0)
        {
            return false;
        }

        return true;
    }

    bool readMpu6050Accel(float &ax, float &ay, float &az)
    {
        Wire.beginTransmission(IMU_ADDRESS);
        Wire.write(IMU_REG_ACCEL_XOUT_H);
        if (Wire.endTransmission(false) != 0)
        {
            return false;
        }

        if (Wire.requestFrom(static_cast<uint8_t>(IMU_ADDRESS), static_cast<size_t>(6)) != 6)
        {
            return false;
        }

        const int16_t rawX = static_cast<int16_t>((Wire.read() << 8) | Wire.read());
        const int16_t rawY = static_cast<int16_t>((Wire.read() << 8) | Wire.read());
        const int16_t rawZ = static_cast<int16_t>((Wire.read() << 8) | Wire.read());

        ax = static_cast<float>(rawX) * ACCEL_SCALE;
        ay = static_cast<float>(rawY) * ACCEL_SCALE;
        az = static_cast<float>(rawZ) * ACCEL_SCALE;

        return true;
    }

    void sensorSamplingTask(void *param)
    {
        (void)param;
        uint32_t nextSampleTime = micros();

        while (true)
        {
            const uint32_t now = micros();
            const int32_t diff = static_cast<int32_t>(now - nextSampleTime);

            if (diff >= 0)
            {
                nextSampleTime += SAMPLE_PERIOD_US;

                float ax = 0.0f, ay = 0.0f, az = 0.0f;
                if (readMpu6050Accel(ax, ay, az))
                {
                    consecutiveI2CFailures = 0;

                    bool notifyInference = false;

                    portENTER_CRITICAL(&windowMux);
                    if (windowManagerAddSample(ax, ay, az) && !windowPending && inferenceTaskHandle != nullptr)
                    {
                        windowPending = true;
                        notifyInference = true;
                    }
                    portEXIT_CRITICAL(&windowMux);

                    if (notifyInference)
                    {
                        xTaskNotifyGive(inferenceTaskHandle);
                    }
                }
                else
                {
                    consecutiveI2CFailures++;
                    Serial.print(F("I2C read failure #"));
                    Serial.println(consecutiveI2CFailures);

                    if (consecutiveI2CFailures >= I2C_FAILURE_THRESHOLD)
                    {
                        sensorFaultLatched = true;
                        Serial.println(F("FATAL: MPU6050 I2C failure threshold reached. Halting sensor sampling."));
                        Serial.flush();
                        if (inferenceTaskHandle != nullptr)
                        {
                            xTaskNotifyGive(inferenceTaskHandle);
                        }
                        while (true)
                        {
                            vTaskDelay(pdMS_TO_TICKS(1000));
                        }
                    }
                }
            }
            else
            {
                const int32_t remainingUs = -diff;
                if (remainingUs > 1200)
                {
                    vTaskDelay(1);
                }
                else
                {
                    delayMicroseconds(50);
                }
            }
        }
    }
} // namespace

void setup()
{
    Serial.begin(115200);
    while (!Serial && millis() < 2000)
        ;

    Serial.println(F("\n--- STARTING ANOMALY DETECTION SYSTEM (cAE INT8) ---"));

    initDsp();
    initWindowManager();

    if (!initModelRunner())
    {
        Serial.println(F("ERROR: Failed to initialize TFLM Model Runner!"));
        while (true)
        {
            delay(1000);
        }
    }

    applyEnvironmentConfig(currentEnv);

    if (!initMpu6050())
    {
        Serial.println(F("ERROR: MPU6050 not found. Please verify I2C wiring!"));
        while (true)
        {
            delay(1000);
        }
    }
    Serial.println(F("MPU6050 initialized successfully in +/-16g mode, DLPF=44Hz, ODR=500Hz."));

    memset(anomalyRing, 0, sizeof(anomalyRing));
    anomalyRingIndex = 0;
    anomalyCount = 0;
    lastAnomalyTimestamp = 0;
    alarmActive = false;

    inferenceTaskHandle = xTaskGetCurrentTaskHandle();

    xTaskCreatePinnedToCore(
        sensorSamplingTask,
        "SensorTask",
        4096,
        nullptr,
        2,
        nullptr,
        0);

    Serial.print(F("Starting Empirical Percentile calibration ("));
    Serial.print(CALIBRATION_WINDOW_COUNT);
    Serial.println(F(" windows)..."));

    if (!initMqtt())
    {
        Serial.println(F("[MQTT] Setup warning: MQTT initialization deferred or offline."));
    }
}

void loop()
{
    // await notification from core 0 with bounded timeout so MQTT and heartbeats remain active if sensor stalls
    const uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));

    mqttLoop();

    if (sensorFaultLatched && !sensorFaultAlertSent)
    {
        sensorFaultAlertSent = true;
        AlertData faultAlert = {};
        faultAlert.event = "SENSOR_FAULT";
        faultAlert.alarmActive = true;
        faultAlert.alarmLatchMs = ALARM_LATCH_MS;
        faultAlert.dynamicThreshold = getDynamicThreshold();
        faultAlert.windowSpan = ANOMALY_WINDOW_SPAN;
        faultAlert.triggerThreshold = ANOMALY_COUNT_THRESHOLD;
        faultAlert.resetThresholdWindows = EPISODE_RESET_NORMAL;
        faultAlert.architecture = "cAE INT8";
        faultAlert.environment = getEnvName(currentEnv);
        faultAlert.latentDim = LATENT_DIM;
        publishAlert(&faultAlert);
    }

    const uint32_t nowMs = millis();
    if (nowMs - lastHeartbeatTime >= HEARTBEAT_INTERVAL_MS)
    {
        lastHeartbeatTime = nowMs;
        HeartbeatData hb;
        hb.systemState = sensorFaultLatched
                             ? "SENSOR_FAULT"
                             : ((getRunnerState() == STATE_MONITORING) ? "MONITORING" : "CALIBRATING");
        hb.environment = getEnvName(currentEnv);
        hb.uptimeSeconds = nowMs / 1000;
        hb.isCalibrated = (getRunnerState() == STATE_MONITORING);
        hb.dynamicThreshold = getDynamicThreshold();
        hb.calibrationWindows = CALIBRATION_WINDOW_COUNT;
        hb.i2cConsecutiveFailures = consecutiveI2CFailures;
        hb.i2cFailureThreshold = I2C_FAILURE_THRESHOLD;
        hb.mpu6050OdrHz = SAMPLING_RATE_HZ;
        hb.mpu6050RangeG = 16;
        hb.mpu6050DlpfHz = 44;
        hb.freeHeapBytes = ESP.getFreeHeap();
        hb.minFreeHeapBytes = ESP.getMinFreeHeap();
        hb.core0Task = "SensorTask";
        hb.core1Task = "InferenceTask";
        publishHeartbeat(&hb);
    }

    if (notified == 0 || sensorFaultLatched)
    {
        return;
    }

    // start of per-window processing pipeline.
    const uint32_t tSnapshotStart = micros();

    // thread-safe snapshot of the most recent 512 samples
    bool snapshotOk = false;
    uint32_t newestSampleTs = 0;
    portENTER_CRITICAL(&windowMux);
    snapshotOk = windowManagerSnapshot(windowSnapshot[AXIS_X],
                                       windowSnapshot[AXIS_Y],
                                       windowSnapshot[AXIS_Z]);
    newestSampleTs = windowManagerGetLastSampleTimestamp();
    windowPending = false;
    portEXIT_CRITICAL(&windowMux);

    if (!snapshotOk)
    {
        return;
    }

    // To apply dsp engineering (reuse windowSnapshot in-place; model reads bins 11-138).
    // shortcut: in-place FFT output aliases the start of windowSnapshot; do not read raw time-series after this
    float *const magX = windowSnapshot[AXIS_X];
    float *const magY = windowSnapshot[AXIS_Y];
    float *const magZ = windowSnapshot[AXIS_Z];
    computeFFTMagnitude(windowSnapshot[AXIS_X], magX, WINDOW_SIZE);
    computeFFTMagnitude(windowSnapshot[AXIS_Y], magY, WINDOW_SIZE);
    computeFFTMagnitude(windowSnapshot[AXIS_Z], magZ, WINDOW_SIZE);

    // To apply square-root compression and L2-normalization
    prepareModelInput(magX, magY, magZ, modelInput);
    const uint32_t tAfterDsp = micros();

    // To run INT8 Autoencoder inference
    const bool wasCalibrating = (getRunnerState() == STATE_CALIBRATING);
    float currentMse = 0.0f;
    bool isWindowAnomaly = false;
    const InferenceStatus infStatus = runInference(modelInput, conditionVector, &currentMse, &isWindowAnomaly);
    if (infStatus != INFERENCE_OK)
    {
        Serial.print(F("[ERROR] Inference failed with status: "));
        Serial.println(static_cast<int>(infStatus));
        return;
    }
    const RunnerState stateAfter = getRunnerState();

    const uint32_t tAfterInference = micros();

    // latency calculation.
    const uint32_t dspUs = tAfterDsp - tSnapshotStart;
    const uint32_t inferenceUs = tAfterInference - tAfterDsp;
    const uint32_t e2eUs = tAfterInference - newestSampleTs;

    if (wasCalibrating)
    {
        const size_t calStep = getCalibrationStep();
        Serial.print(F("[CALIBRATION "));
        Serial.print(calStep);
        Serial.print(F("/"));
        Serial.print(CALIBRATION_WINDOW_COUNT);
        Serial.print(F("] MSE: "));
        Serial.print(currentMse, 6);
        Serial.print(F(" | Lat(us) dsp="));
        Serial.print(dspUs);
        Serial.print(F(" inf="));
        Serial.print(inferenceUs);
        Serial.print(F(" e2e="));
        Serial.println(e2eUs);

        TelemetryData telem;
        telem.systemState = "CALIBRATING";
        telem.environment = getEnvName(currentEnv);
        telem.isCalibrating = true;
        telem.calibrationStep = calStep;
        telem.totalCalibrationSteps = CALIBRATION_WINDOW_COUNT;
        telem.currentMse = currentMse;
        telem.dynamicThreshold = 0.0f;
        telem.isWindowAnomaly = isWindowAnomaly;
        telem.anomalyCount = 0;
        telem.windowSpan = ANOMALY_WINDOW_SPAN;
        telem.triggerThreshold = ANOMALY_COUNT_THRESHOLD;
        telem.dspUs = dspUs;
        telem.inferenceUs = inferenceUs;
        telem.e2eUs = e2eUs;
        telem.magX = magX;
        telem.magY = magY;
        telem.magZ = magZ;
        telem.startBin = FEATURE_START_BIN;
        telem.binCount = FEATURE_COUNT;
        telem.freqStartHz = static_cast<float>(FEATURE_START_BIN) * (500.0f / 512.0f);
        telem.freqEndHz = static_cast<float>(FEATURE_END_BIN) * (500.0f / 512.0f);
        publishTelemetry(&telem);

        if (stateAfter == STATE_MONITORING)
        {
            Serial.print(F(">>> CALIBRATION COMPLETE! Dynamic Threshold = "));
            Serial.println(getDynamicThreshold(), 6);
        }
        return;
    }

    // update anomaly decision ring.
    const uint32_t now = millis();
    const uint8_t newVerdict = isWindowAnomaly ? 1u : 0u;

    anomalyCount -= anomalyRing[anomalyRingIndex];
    anomalyRing[anomalyRingIndex] = newVerdict;
    anomalyCount += newVerdict;

    anomalyRingIndex++;
    if (anomalyRingIndex >= ANOMALY_WINDOW_SPAN)
    {
        anomalyRingIndex = 0;
    }

    if (newVerdict)
    {
        lastAnomalyTimestamp = now;
    }

    // the clear fauls logic.
    if (newVerdict)
    {
        if (!episodeActive)
        {
            episodeActive = true;
            faultStartTimestampMs = now;
        }
        consecutiveNormalWindows = 0;
    }
    else
    {
        if (episodeActive)
        {
            consecutiveNormalWindows++;
            if (consecutiveNormalWindows >= EPISODE_RESET_NORMAL)
            {
                episodeActive = false;
                consecutiveNormalWindows = 0;
            }
        }
    }

    // trigger when enough anomalies.
    const bool thresholdReached = (anomalyCount >= ANOMALY_COUNT_THRESHOLD);

    if (thresholdReached)
    {
        if (!alarmActive)
        {
            alarmActive = true;
            peakMse = currentMse;

            const float threshold = getDynamicThreshold();
            Serial.print(F("[MONITORING] MSE: "));
            Serial.print(currentMse, 6);
            Serial.print(F(" | Threshold: "));
            Serial.print(threshold, 6);
            Serial.print(F(" | Anomalies: "));
            Serial.print(anomalyCount);
            Serial.print(F("/"));
            Serial.print(ANOMALY_WINDOW_SPAN);
            Serial.println(F(" | >>> ANOMALY DETECTED (ALARM LATCHED) <<<"));

            const int8_t *latentVec = getLatentVector();
            if (latentVec != nullptr)
            {
                Serial.print(F("    [LATENT 16D]: ["));
                for (size_t i = 0; i < LATENT_DIM; i++)
                {
                    Serial.print(latentVec[i]);
                    if (i < LATENT_DIM - 1)
                        Serial.print(F(", "));
                }
                Serial.println(F("]"));
            }

            const uint32_t detectionDelayMs = now - faultStartTimestampMs;
            Serial.print(F("    [DETECTION DELAY ms]: "));
            Serial.println(detectionDelayMs);

            Serial.print(F("    [LATENCY us] dsp="));
            Serial.print(dspUs);
            Serial.print(F(" inf="));
            Serial.print(inferenceUs);
            Serial.print(F(" e2e="));
            Serial.println(e2eUs);

            AlertData alertData;
            alertData.event = "ALARM_LATCHED";
            alertData.alarmActive = true;
            alertData.alarmLatchMs = ALARM_LATCH_MS;
            alertData.currentMse = currentMse;
            alertData.peakMse = peakMse;
            alertData.dynamicThreshold = threshold;
            alertData.anomalyCount = anomalyCount;
            alertData.windowSpan = ANOMALY_WINDOW_SPAN;
            alertData.triggerThreshold = ANOMALY_COUNT_THRESHOLD;
            alertData.detectionDelayMs = detectionDelayMs;
            alertData.consecutiveNormalWindows = consecutiveNormalWindows;
            alertData.resetThresholdWindows = EPISODE_RESET_NORMAL;
            alertData.architecture = "cAE INT8";
            alertData.environment = getEnvName(currentEnv);
            alertData.latentDim = LATENT_DIM;
            alertData.latentVector = latentVec;
            alertData.dspUs = dspUs;
            alertData.inferenceUs = inferenceUs;
            alertData.e2eUs = e2eUs;
            publishAlert(&alertData);
        }
        else
        {
            if (currentMse > peakMse)
            {
                peakMse = currentMse;
            }
        }
    }
    else
    {
        if (alarmActive && (now - lastAnomalyTimestamp >= ALARM_LATCH_MS))
        {
            alarmActive = false;
        }

        if (!alarmActive)
        {
            const float threshold = getDynamicThreshold();
            Serial.print(F("[MONITORING] MSE: "));
            Serial.print(currentMse, 6);
            Serial.print(F(" | Threshold: "));
            Serial.print(threshold, 6);
            Serial.print(F(" | Anomalies: "));
            Serial.print(anomalyCount);
            Serial.print(F("/"));
            Serial.print(ANOMALY_WINDOW_SPAN);
            Serial.print(F(" | Lat(us) dsp="));
            Serial.print(dspUs);
            Serial.print(F(" inf="));
            Serial.print(inferenceUs);
            Serial.print(F(" e2e="));
            Serial.print(e2eUs);
            Serial.println(F(" | Status: Normal"));
        }
    }

    TelemetryData telem;
    telem.systemState = "MONITORING";
    telem.environment = getEnvName(currentEnv);
    telem.isCalibrating = false;
    telem.calibrationStep = CALIBRATION_WINDOW_COUNT;
    telem.totalCalibrationSteps = CALIBRATION_WINDOW_COUNT;
    telem.currentMse = currentMse;
    telem.dynamicThreshold = getDynamicThreshold();
    telem.isWindowAnomaly = isWindowAnomaly;
    telem.anomalyCount = anomalyCount;
    telem.windowSpan = ANOMALY_WINDOW_SPAN;
    telem.triggerThreshold = ANOMALY_COUNT_THRESHOLD;
    telem.dspUs = dspUs;
    telem.inferenceUs = inferenceUs;
    telem.e2eUs = e2eUs;
    telem.magX = magX;
    telem.magY = magY;
    telem.magZ = magZ;
    telem.startBin = FEATURE_START_BIN;
    telem.binCount = FEATURE_COUNT;
    telem.freqStartHz = static_cast<float>(FEATURE_START_BIN) * (500.0f / 512.0f);
    telem.freqEndHz = static_cast<float>(FEATURE_END_BIN) * (500.0f / 512.0f);
    publishTelemetry(&telem);
}
