#include <Arduino.h>
#include <Wire.h>
#include <string.h>

#include "dsp_edge.h"
#include "window_manager.h"
#include "model_runner.h"

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

    // Thread-safe snapshot buffer for DSP execution on core 1
    float windowSnapshot[AXIS_COUNT][WINDOW_SIZE];

    // Notification throttle
    volatile bool windowPending = false;

    // FFT single-sided amplitude spectrum buffers (257 bins per axis)
    float magX[FFT_BINS];
    float magY[FFT_BINS];
    float magZ[FFT_BINS];

    float modelInput[MODEL_INPUT_SIZE];
    float conditionVector[2];

    const Environment currentEnv = ENV_TABLE;

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
    size_t calibrationStepCounter = 0;

    // To reset the saved states if anomalies (caused by users' iteraction rather than real fauls) end.
    constexpr uint32_t EPISODE_RESET_NORMAL = 3;
    uint32_t faultStartTimestampMs = 0;
    uint32_t consecutiveNormalWindows = 0;
    bool episodeActive = false;

    // I2C failure handling
    constexpr uint32_t I2C_FAILURE_THRESHOLD = 10;
    uint32_t consecutiveI2CFailures = 0;

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

                    bool windowReady = false;

                    portENTER_CRITICAL(&windowMux);
                    windowReady = windowManagerAddSample(ax, ay, az);
                    portEXIT_CRITICAL(&windowMux);

                    if (windowReady && !windowPending && inferenceTaskHandle != nullptr)
                    {
                        windowPending = true;
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
                        Serial.println(F("FATAL: MPU6050 I2C failure threshold reached. Halting system."));
                        Serial.flush();
                        while (true)
                        {
                            delay(1000);
                            Serial.println(F("System halted due to I2C failure."));
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

    prepareConditionVector(currentEnv, conditionVector);

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
}

void loop()
{
    // await notification from core 0 if a new window is ready.
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

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

    // To apply dsp engineering.
    computeFFTMagnitude(windowSnapshot[AXIS_X], magX, WINDOW_SIZE);
    computeFFTMagnitude(windowSnapshot[AXIS_Y], magY, WINDOW_SIZE);
    computeFFTMagnitude(windowSnapshot[AXIS_Z], magZ, WINDOW_SIZE);

    // To apply square-root compression and L2-normalization
    prepareModelInput(magX, magY, magZ, modelInput);
    const uint32_t tAfterDsp = micros();

    // To run INT8 Autoencoder inference
    const bool wasCalibrating = (getRunnerState() == STATE_CALIBRATING);
    float currentMse = 0.0f;
    const bool isWindowAnomaly = runInference(modelInput, conditionVector, &currentMse);
    const RunnerState stateAfter = getRunnerState();

    const uint32_t tAfterInference = micros();

    // latency calculation.
    const uint32_t dspUs = tAfterDsp - tSnapshotStart;
    const uint32_t inferenceUs = tAfterInference - tAfterDsp;
    const uint32_t e2eUs = tAfterInference - newestSampleTs;

    if (wasCalibrating)
    {
        calibrationStepCounter++;
        Serial.print(F("[CALIBRATION "));
        Serial.print(calibrationStepCounter);
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
}