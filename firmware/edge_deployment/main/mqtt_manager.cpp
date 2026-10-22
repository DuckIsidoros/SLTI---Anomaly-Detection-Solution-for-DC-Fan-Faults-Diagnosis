#include "mqtt_manager.h"
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>

#include "esp_idf_version.h"
#include "mqtt_client.h"

namespace
{
    esp_mqtt_client_handle_t s_mqttClient = nullptr;
    volatile bool s_connected = false;
    bool s_mqttStarted = false;
    uint32_t s_lastHeartbeatMs = 0;

    // Shared static buffer in .bss for Core 1 JSON serialization (max telemetry ~3.5 KB)
    // shortcut: 4096B shared across sequential Core 1 publishers, guard with mutex if called from multiple tasks
    char s_jsonBuffer[4096];
    char s_lwtBuffer[256];

    void getIso8601Timestamp(char *dest, size_t maxLen)
    {
        time_t nowSec = time(nullptr);
        if (nowSec > 1600000000)
        {
            struct tm timeinfo;
            gmtime_r(&nowSec, &timeinfo);
            const uint32_t ms = millis() % 1000;
            snprintf(dest, maxLen, "%04d-%02d-%02dT%02d:%02d:%02d.%03luZ",
                     timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                     static_cast<unsigned long>(ms));
        }
        else
        {
            const uint32_t totalSec = millis() / 1000;
            const uint32_t ms = millis() % 1000;
            const uint32_t hours = (totalSec / 3600) % 24;
            const uint32_t mins = (totalSec / 60) % 60;
            const uint32_t secs = totalSec % 60;
            snprintf(dest, maxLen, "2026-10-20T%02lu:%02lu:%02lu.%03luZ",
                     static_cast<unsigned long>(hours),
                     static_cast<unsigned long>(mins),
                     static_cast<unsigned long>(secs),
                     static_cast<unsigned long>(ms));
        }
    }

    class JsonWriter
    {
    public:
        JsonWriter(char *buf, size_t cap)
            : m_buf(buf), m_cap(cap), m_len(0)
        {
            if (m_cap > 0)
            {
                m_buf[0] = '\0';
            }
        }

        bool append(const char *str)
        {
            if (!str)
            {
                return true;
            }
            const size_t slen = strlen(str);
            if (m_len + slen >= m_cap)
            {
                return false;
            }
            memcpy(m_buf + m_len, str, slen);
            m_len += slen;
            m_buf[m_len] = '\0';
            return true;
        }

        bool appendFormat(const char *fmt, ...)
        {
            if (m_len >= m_cap)
            {
                return false;
            }
            va_list args;
            va_start(args, fmt);
            const int written = vsnprintf(m_buf + m_len, m_cap - m_len, fmt, args);
            va_end(args);
            if (written > 0)
            {
                const size_t w = static_cast<size_t>(written);
                if (m_len + w >= m_cap)
                {
                    m_len = m_cap - 1;
                    m_buf[m_len] = '\0';
                    return false;
                }
                m_len += w;
                return true;
            }
            return false;
        }

        bool appendFloatArray(const float *arr, size_t count, int precision = 4)
        {
            if (!append("["))
            {
                return false;
            }
            char numBuf[32];
            for (size_t i = 0; i < count; ++i)
            {
                dtostrf(arr[i], 1, precision, numBuf);
                if (!append(numBuf))
                {
                    return false;
                }
                if (i + 1 < count)
                {
                    if (!append(","))
                    {
                        return false;
                    }
                }
            }
            return append("]");
        }

        bool appendInt8Array(const int8_t *arr, size_t count)
        {
            if (!append("["))
            {
                return false;
            }
            char numBuf[16];
            for (size_t i = 0; i < count; ++i)
            {
                snprintf(numBuf, sizeof(numBuf), "%d", static_cast<int>(arr[i]));
                if (!append(numBuf))
                {
                    return false;
                }
                if (i + 1 < count)
                {
                    if (!append(","))
                    {
                        return false;
                    }
                }
            }
            return append("]");
        }

        const char *c_str() const
        {
            return m_buf;
        }

        size_t length() const
        {
            return m_len;
        }

    private:
        char *m_buf;
        size_t m_cap;
        size_t m_len;
    };

    void publishStatus(const char *status)
    {
        if (s_mqttClient == nullptr || !s_connected)
        {
            return;
        }

        char ts[32];
        getIso8601Timestamp(ts, sizeof(ts));

        char statusBuffer[256];
        snprintf(statusBuffer, sizeof(statusBuffer),
                 "{\"device_id\":\"%s\",\"timestamp\":\"%s\",\"msg_type\":\"status\",\"status\":\"%s\"}",
                 DEVICE_ID, ts, status);

        esp_mqtt_client_publish(s_mqttClient, MQTT_TOPIC_STATUS, statusBuffer, 0, MQTT_QOS_STATUS, 1);
    }

    void mqttEventHandler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
    {
        (void)handler_args;
        (void)base;
        (void)event_data;

        switch (static_cast<esp_mqtt_event_id_t>(event_id))
        {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            Serial.println(F("[MQTT] Connected to broker successfully."));
            publishStatus("ONLINE");
            break;

        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            Serial.println(F("[MQTT] Disconnected from broker."));
            break;

        case MQTT_EVENT_ERROR:
            Serial.println(F("[MQTT] Event error encountered."));
            break;

        default:
            break;
        }
    }
} // namespace

bool initMqtt(void)
{
    Serial.println(F("[WIFI] Connecting to network..."));
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const uint32_t wifiStartMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - wifiStartMs) < 10000)
    {
        delay(250);
    }

    // Prepare Last Will and Testament (LWT) payload
    char initialTs[32];
    getIso8601Timestamp(initialTs, sizeof(initialTs));
    snprintf(s_lwtBuffer, sizeof(s_lwtBuffer),
             "{\"device_id\":\"%s\",\"timestamp\":\"%s\",\"msg_type\":\"status\",\"status\":\"OFFLINE\",\"reason\":\"connection_lost\"}",
             DEVICE_ID, initialTs);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.hostname = MQTT_BROKER_HOST;
    mqtt_cfg.broker.address.port = MQTT_BROKER_PORT;
#if MQTT_USE_TLS
    mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
    mqtt_cfg.broker.verification.certificate = ROOT_CA_CERT_PEM;
    mqtt_cfg.broker.verification.skip_cert_common_name_check = true;
#else
    mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
#endif
    mqtt_cfg.credentials.client_id = MQTT_CLIENT_ID;
    mqtt_cfg.credentials.username = MQTT_USERNAME;
    mqtt_cfg.credentials.authentication.password = MQTT_PASSWORD;
    mqtt_cfg.session.keepalive = 60;
    mqtt_cfg.session.last_will.topic = MQTT_TOPIC_STATUS;
    mqtt_cfg.session.last_will.msg = s_lwtBuffer;
    mqtt_cfg.session.last_will.msg_len = strlen(s_lwtBuffer);
    mqtt_cfg.session.last_will.qos = MQTT_QOS_STATUS;
    mqtt_cfg.session.last_will.retain = 1;
    mqtt_cfg.buffer.size = 4096;
    mqtt_cfg.buffer.out_size = 4096;
#else
    esp_mqtt_client_config_t mqtt_cfg;
    memset(&mqtt_cfg, 0, sizeof(mqtt_cfg));
    mqtt_cfg.host = MQTT_BROKER_HOST;
    mqtt_cfg.port = MQTT_BROKER_PORT;
#if MQTT_USE_TLS
    mqtt_cfg.transport = MQTT_TRANSPORT_OVER_SSL;
    mqtt_cfg.cert_pem = ROOT_CA_CERT_PEM;
    mqtt_cfg.skip_cert_common_name_check = true;
#else
    mqtt_cfg.transport = MQTT_TRANSPORT_OVER_TCP;
#endif
    mqtt_cfg.client_id = MQTT_CLIENT_ID;
    mqtt_cfg.username = MQTT_USERNAME;
    mqtt_cfg.password = MQTT_PASSWORD;
    mqtt_cfg.keepalive = 60;
    mqtt_cfg.lwt_topic = MQTT_TOPIC_STATUS;
    mqtt_cfg.lwt_msg = s_lwtBuffer;
    mqtt_cfg.lwt_msg_len = strlen(s_lwtBuffer);
    mqtt_cfg.lwt_qos = MQTT_QOS_STATUS;
    mqtt_cfg.lwt_retain = 1;
    mqtt_cfg.buffer_size = 4096;
    mqtt_cfg.out_buffer_size = 4096;
#endif

    s_mqttClient = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqttClient == nullptr)
    {
        Serial.println(F("[MQTT] Failed to initialize MQTT client handle!"));
        return false;
    }

    esp_mqtt_client_register_event(s_mqttClient, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID), mqttEventHandler, nullptr);

    if (WiFi.status() != WL_CONNECTED)
    {
        return false;
    }

    mqttLoop();
    return s_mqttStarted;
}

void mqttLoop(void)
{
    // Start NTP and MQTT client task only after Wi-Fi station has acquired an IP route
    if (!s_mqttStarted && s_mqttClient != nullptr && WiFi.status() == WL_CONNECTED)
    {
        Serial.print(F("[WIFI] Connected, IP: "));
        Serial.println(WiFi.localIP());
        configTime(0, 0, "pool.ntp.org", "time.google.com");

        esp_err_t startErr = esp_mqtt_client_start(s_mqttClient);
        if (startErr != ESP_OK)
        {
            Serial.print(F("[MQTT] Failed to start MQTT client task, err = "));
            Serial.println(startErr);
            return;
        }
        s_mqttStarted = true;
        Serial.println(F("[MQTT] Initialized asynchronous MQTT client task."));
    }
}

bool isMqttConnected(void)
{
    return s_connected;
}

bool publishTelemetry(const TelemetryData *data)
{
    if (s_mqttClient == nullptr || !s_connected || data == nullptr)
    {
        return false;
    }

    char ts[32];
    getIso8601Timestamp(ts, sizeof(ts));

    JsonWriter w(s_jsonBuffer, sizeof(s_jsonBuffer));
    w.appendFormat("{\"device_id\":\"%s\",\"timestamp\":\"%s\",\"msg_type\":\"telemetry\",\"payload\":{",
                   DEVICE_ID, ts);
    w.appendFormat("\"system_state\":\"%s\",", data->systemState ? data->systemState : "UNKNOWN");
    w.appendFormat("\"environment\":\"%s\",", data->environment ? data->environment : "UNKNOWN");

    w.appendFormat("\"calibration\":{\"is_calibrating\":%s,\"step\":%lu,\"total_steps\":%lu},",
                   data->isCalibrating ? "true" : "false",
                   static_cast<unsigned long>(data->calibrationStep),
                   static_cast<unsigned long>(data->totalCalibrationSteps));

    char mseBuf[24];
    char threshBuf[24];
    dtostrf(data->currentMse, 1, 6, mseBuf);
    dtostrf(data->dynamicThreshold, 1, 6, threshBuf);

    w.appendFormat("\"inference\":{\"current_mse\":%s,\"dynamic_threshold\":%s,\"is_window_anomaly\":%s},",
                   mseBuf, threshBuf, data->isWindowAnomaly ? "true" : "false");

    w.appendFormat("\"anomaly_ring\":{\"anomaly_count\":%lu,\"window_span\":%lu,\"trigger_threshold\":%lu},",
                   static_cast<unsigned long>(data->anomalyCount),
                   static_cast<unsigned long>(data->windowSpan),
                   static_cast<unsigned long>(data->triggerThreshold));

    w.appendFormat("\"latency_us\":{\"dsp\":%lu,\"inference\":%lu,\"e2e\":%lu},",
                   static_cast<unsigned long>(data->dspUs),
                   static_cast<unsigned long>(data->inferenceUs),
                   static_cast<unsigned long>(data->e2eUs));

    char freqStartBuf[16];
    char freqEndBuf[16];
    dtostrf(data->freqStartHz, 1, 2, freqStartBuf);
    dtostrf(data->freqEndHz, 1, 2, freqEndBuf);

    w.appendFormat("\"fft\":{\"unit\":\"g\",\"window_size\":512,\"sampling_rate_hz\":500,\"delta_f_hz\":0.9765625,\"bin_count\":%lu,\"freq_start_hz\":%s,\"freq_end_hz\":%s,\"axes\":{",
                   static_cast<unsigned long>(data->binCount), freqStartBuf, freqEndBuf);
    w.append("\"x\":");
    if (data->magX != nullptr)
    {
        w.appendFloatArray(data->magX + data->startBin, data->binCount, 4);
    }
    else
    {
        w.append("[]");
    }

    w.append(",\"y\":");
    if (data->magY != nullptr)
    {
        w.appendFloatArray(data->magY + data->startBin, data->binCount, 4);
    }
    else
    {
        w.append("[]");
    }

    w.append(",\"z\":");
    if (data->magZ != nullptr)
    {
        w.appendFloatArray(data->magZ + data->startBin, data->binCount, 4);
    }
    else
    {
        w.append("[]");
    }

    w.append("}}}}");

    const int msgId = esp_mqtt_client_publish(s_mqttClient, MQTT_TOPIC_TELEMETRY, w.c_str(), w.length(), MQTT_QOS_TELEMETRY, 0);
    return (msgId >= 0);
}

bool publishAlert(const AlertData *data)
{
    if (s_mqttClient == nullptr || !s_connected || data == nullptr)
    {
        return false;
    }

    char ts[32];
    getIso8601Timestamp(ts, sizeof(ts));

    char curMseBuf[24];
    char peakMseBuf[24];
    char threshBuf[24];
    dtostrf(data->currentMse, 1, 6, curMseBuf);
    dtostrf(data->peakMse, 1, 6, peakMseBuf);
    dtostrf(data->dynamicThreshold, 1, 6, threshBuf);

    JsonWriter w(s_jsonBuffer, sizeof(s_jsonBuffer));
    w.appendFormat("{\"device_id\":\"%s\",\"timestamp\":\"%s\",\"msg_type\":\"alert\",\"payload\":{",
                   DEVICE_ID, ts);
    w.appendFormat("\"event\":\"%s\",\"alarm_active\":%s,\"alarm_latch_ms\":%lu,",
                   data->event ? data->event : "ALARM_LATCHED",
                   data->alarmActive ? "true" : "false",
                   static_cast<unsigned long>(data->alarmLatchMs));

    w.appendFormat("\"incident_metrics\":{\"current_mse\":%s,\"peak_mse\":%s,\"dynamic_threshold\":%s,\"anomaly_count\":%lu,\"window_span\":%lu,\"trigger_threshold\":%lu,\"detection_delay_ms\":%lu,\"consecutive_normal_windows\":%lu,\"reset_threshold_windows\":%lu},",
                   curMseBuf, peakMseBuf, threshBuf,
                   static_cast<unsigned long>(data->anomalyCount),
                   static_cast<unsigned long>(data->windowSpan),
                   static_cast<unsigned long>(data->triggerThreshold),
                   static_cast<unsigned long>(data->detectionDelayMs),
                   static_cast<unsigned long>(data->consecutiveNormalWindows),
                   static_cast<unsigned long>(data->resetThresholdWindows));

    w.appendFormat("\"edge_model\":{\"architecture\":\"%s\",\"environment\":\"%s\",\"latent_dim\":%lu,\"latent_vector\":",
                   data->architecture ? data->architecture : "cAE INT8",
                   data->environment ? data->environment : "ENV_TABLE",
                   static_cast<unsigned long>(data->latentDim));

    if (data->latentVector != nullptr)
    {
        w.appendInt8Array(data->latentVector, data->latentDim);
    }
    else
    {
        w.append("[]");
    }
    w.append("},");

    w.appendFormat("\"latency_us\":{\"dsp\":%lu,\"inference\":%lu,\"e2e\":%lu}",
                   static_cast<unsigned long>(data->dspUs),
                   static_cast<unsigned long>(data->inferenceUs),
                   static_cast<unsigned long>(data->e2eUs));

    w.append("}}");

    const int msgId = esp_mqtt_client_publish(s_mqttClient, MQTT_TOPIC_ALERT, w.c_str(), w.length(), MQTT_QOS_ALERT, 0);
    return (msgId >= 0);
}

bool publishHeartbeat(const HeartbeatData *data)
{
    if (s_mqttClient == nullptr || !s_connected || data == nullptr)
    {
        return false;
    }

    char ts[32];
    getIso8601Timestamp(ts, sizeof(ts));

    char threshBuf[24];
    dtostrf(data->dynamicThreshold, 1, 6, threshBuf);

    JsonWriter w(s_jsonBuffer, sizeof(s_jsonBuffer));
    w.appendFormat("{\"device_id\":\"%s\",\"timestamp\":\"%s\",\"msg_type\":\"heartbeat\",\"payload\":{",
                   DEVICE_ID, ts);
    w.appendFormat("\"system_state\":\"%s\",\"environment\":\"%s\",\"uptime_seconds\":%lu,",
                   data->systemState ? data->systemState : "UNKNOWN",
                   data->environment ? data->environment : "UNKNOWN",
                   static_cast<unsigned long>(data->uptimeSeconds));

    w.appendFormat("\"calibration\":{\"is_calibrated\":%s,\"dynamic_threshold\":%s,\"calibration_windows\":%lu},",
                   data->isCalibrated ? "true" : "false",
                   threshBuf,
                   static_cast<unsigned long>(data->calibrationWindows));

    w.appendFormat("\"hardware_health\":{\"i2c_consecutive_failures\":%lu,\"i2c_failure_threshold\":%lu,\"mpu6050_odr_hz\":%lu,\"mpu6050_range_g\":%lu,\"mpu6050_dlpf_hz\":%lu},",
                   static_cast<unsigned long>(data->i2cConsecutiveFailures),
                   static_cast<unsigned long>(data->i2cFailureThreshold),
                   static_cast<unsigned long>(data->mpu6050OdrHz),
                   static_cast<unsigned long>(data->mpu6050RangeG),
                   static_cast<unsigned long>(data->mpu6050DlpfHz));

    w.appendFormat("\"system_resources\":{\"free_heap_bytes\":%lu,\"min_free_heap_bytes\":%lu,\"core0_task\":\"%s\",\"core1_task\":\"%s\"}",
                   static_cast<unsigned long>(data->freeHeapBytes),
                   static_cast<unsigned long>(data->minFreeHeapBytes),
                   data->core0Task ? data->core0Task : "SensorTask",
                   data->core1Task ? data->core1Task : "InferenceTask");

    w.append("}}");

    const int msgId = esp_mqtt_client_publish(s_mqttClient, MQTT_TOPIC_HEARTBEAT, w.c_str(), w.length(), MQTT_QOS_HEARTBEAT, 0);
    return (msgId >= 0);
}
