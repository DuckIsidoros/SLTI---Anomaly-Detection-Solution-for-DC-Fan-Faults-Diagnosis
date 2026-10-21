#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mqtt_config.h"

struct TelemetryData
{
    const char *systemState;
    const char *environment;
    bool isCalibrating;
    uint32_t calibrationStep;
    uint32_t totalCalibrationSteps;
    float currentMse;
    float dynamicThreshold;
    bool isWindowAnomaly;
    uint32_t anomalyCount;
    uint32_t windowSpan;
    uint32_t triggerThreshold;
    uint32_t dspUs;
    uint32_t inferenceUs;
    uint32_t e2eUs;
    const float *magX;
    const float *magY;
    const float *magZ;
    size_t startBin;
    size_t binCount;
    float freqStartHz;
    float freqEndHz;
};

struct AlertData
{
    const char *event;
    bool alarmActive;
    uint32_t alarmLatchMs;
    float currentMse;
    float peakMse;
    float dynamicThreshold;
    uint32_t anomalyCount;
    uint32_t windowSpan;
    uint32_t triggerThreshold;
    uint32_t detectionDelayMs;
    uint32_t consecutiveNormalWindows;
    uint32_t resetThresholdWindows;
    const char *architecture;
    const char *environment;
    size_t latentDim;
    const int8_t *latentVector;
    uint32_t dspUs;
    uint32_t inferenceUs;
    uint32_t e2eUs;
};

struct HeartbeatData
{
    const char *systemState;
    const char *environment;
    uint32_t uptimeSeconds;
    bool isCalibrated;
    float dynamicThreshold;
    uint32_t calibrationWindows;
    uint32_t i2cConsecutiveFailures;
    uint32_t i2cFailureThreshold;
    uint32_t mpu6050OdrHz;
    uint32_t mpu6050RangeG;
    uint32_t mpu6050DlpfHz;
    uint32_t freeHeapBytes;
    uint32_t minFreeHeapBytes;
    const char *core0Task;
    const char *core1Task;
};

#ifdef __cplusplus
extern "C"
{
#endif

    // Initializes WiFi connection, SNTP time synchronization, and asynchronous MQTT client
    bool initMqtt(void);

    // Maintenance loop called from loop() to monitor network connectivity and trigger periodic heartbeats
    void mqttLoop(void);

    // Returns true if MQTT client is currently connected to broker
    bool isMqttConnected(void);

    // Serializes and publishes per-window telemetry payload (QoS 0)
    bool publishTelemetry(const TelemetryData *data);

    // Serializes and publishes anomaly alert payload (QoS 1)
    bool publishAlert(const AlertData *data);

    // Serializes and publishes periodic system heartbeat & status payload (QoS 0)
    bool publishHeartbeat(const HeartbeatData *data);

#ifdef __cplusplus
}
#endif
