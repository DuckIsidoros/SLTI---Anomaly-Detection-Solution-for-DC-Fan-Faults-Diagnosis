# MQTT Topic Hierarchy Architecture

## 1. Topic Structure

Compliant with ISO 13374 for Condition Monitoring and Diagnostics data hierarchy.

- **Format:** `v1/{plant_id}/{line_id}/{machine_id}/{sensor_type}/{msg_type}`
- **Example:** `v1/plant_hcm/line_01/cnc_05/accelerometer/telemetry`

### Topic Level Definitions

- `v1`: Protocol and schema version prefix.
- `{plant_id}`: Site / factory location identifier (e.g., `plant_hcm`).
- `{line_id}`: Production line identifier (e.g., `line_01`).
- `{machine_id}`: Physical machine or asset unit identifier (e.g., `cnc_05`).
- `{sensor_type}`: Physical sensing module or telemetry subsystem (e.g., `accelerometer`, `thermocouple`, `current_clamp`).
- `{msg_type}`: Functional classification of the payload (`telemetry`, `alert`, `heartbeat`, `status`).

---

## 2. Message Types & Quality of Service (QoS)

| Message Type (`msg_type`) | Description                                                                           | QoS | Design Rationale                                                                                                                                                            |
| :------------------------ | :------------------------------------------------------------------------------------ | :-: | :-------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `telemetry`               | Per-window inference metrics, latency breakdowns, and vibration FFT spectrum data     |  0  | At-most-once delivery; streaming per-window telemetry feeds real-time dashboard widgets (MSE, latency charts, FFT spectrum) without transmission queuing or broker backlog. |
| `alert`                   | Edge AI anomaly event notifications and latched alarms                                |  1  | At-least-once delivery; critical operational alerts and audit log entries must guarantee delivery without packet loss via mandatory broker acknowledgment (`PUBACK`).       |
| `heartbeat`               | Device operational health, calibration state, environment profile, and I2C bus status |  0  | Periodic transient health telemetry; dropping an isolated packet has negligible operational impact.                                                                         |

---

## 3. Standard Payload Schema (JSON)

All messages are serialized as UTF-8 encoded JSON. All timestamps must strictly follow the ISO 8601 extended UTC standard (`YYYY-MM-DDTHH:mm:ss.sssZ`).

### 3.1 Telemetry Payload

Published at the end of each inference cycle (per 512-sample window) to power real-time dashboard widgets:

- **System State Banner & Instantaneous MSE**: Live operational state, current MSE, dynamic threshold, and calibration progress.
- **Anomaly Window Gauge**: Density ratio of anomalous windows in the rolling ring buffer (`anomaly_count` out of `window_span`, triggering at threshold).
- **Latency Breakdown Parameters & Charts**: Execution timing in microseconds for DSP preprocessing (`dsp_us`), INT8 model inference (`inference_us`), and end-to-end turnaround (`e2e_us`).
- **FFT Amplitude Spectrum**: 257-bin single-sided amplitude spectrum across X, Y, and Z axes (0–250 Hz at 500 Hz ODR).

```json
{
  "device_id": "esp32_edge_01",
  "timestamp": "2026-10-20T14:10:00.123Z",
  "msg_type": "telemetry",
  "payload": {
    "system_state": "MONITORING",
    "environment": "ENV_TABLE",
    "calibration": {
      "is_calibrating": false,
      "step": 20,
      "total_steps": 20
    },
    "inference": {
      "current_mse": 0.003412,
      "dynamic_threshold": 0.00785,
      "is_window_anomaly": false
    },
    "anomaly_ring": {
      "anomaly_count": 3,
      "window_span": 20,
      "trigger_threshold": 10
    },
    "latency_us": {
      "dsp": 1240,
      "inference": 4850,
      "e2e": 6820
    },
    "fft": {
      "unit": "g",
      "window_size": 512,
      "sampling_rate_hz": 500,
      "delta_f_hz": 0.9765625,
      "bin_count": 257,
      "freq_start_hz": 0.0,
      "freq_end_hz": 250.0,
      "axes": {
        "x": [0.012, 0.045, 0.089, 0.014],
        "y": [0.01, 0.038, 0.075, 0.011],
        "z": [0.025, 0.12, 0.45, 0.032]
      }
    }
  }
}
```

### 3.2 Alert Payload

Generated immediately when an anomaly condition trips the 10/20 window threshold (`anomalyCount >= ANOMALY_COUNT_THRESHOLD`), latching the alarm for at least 3,000 ms (`ALARM_LATCH_MS`). This schema directly populates the dashboard's **Alarm Audit Log** and incident response workflows.

```json
{
  "device_id": "esp32_edge_01",
  "timestamp": "2026-10-20T14:15:22.456Z",
  "msg_type": "alert",
  "payload": {
    "event": "ALARM_LATCHED",
    "alarm_active": true,
    "alarm_latch_ms": 3000,
    "incident_metrics": {
      "current_mse": 0.01845,
      "peak_mse": 0.021543,
      "dynamic_threshold": 0.00785,
      "anomaly_count": 12,
      "window_span": 20,
      "trigger_threshold": 10,
      "detection_delay_ms": 1240,
      "consecutive_normal_windows": 0,
      "reset_threshold_windows": 3
    },
    "edge_model": {
      "architecture": "cAE INT8",
      "environment": "ENV_TABLE",
      "latent_dim": 16,
      "latent_vector": [
        -18, 42, -5, 12, 0, -33, 89, -14, 27, -8, 64, -2, 19, -45, 11, -7
      ]
    },
    "latency_us": {
      "dsp": 1248,
      "inference": 4872,
      "e2e": 6845
    }
  }
}
```

### 3.3 Heartbeat & Status Payload

Published periodically (e.g., every 30–60 seconds) or during system initialization/state transitions to track device availability, MPU6050 I2C bus health, and runtime diagnostics.

```json
{
  "device_id": "esp32_edge_01",
  "timestamp": "2026-10-20T14:10:00.000Z",
  "msg_type": "heartbeat",
  "payload": {
    "system_state": "MONITORING",
    "environment": "ENV_TABLE",
    "uptime_seconds": 86400,
    "calibration": {
      "is_calibrated": true,
      "dynamic_threshold": 0.00785,
      "calibration_windows": 20
    },
    "hardware_health": {
      "i2c_consecutive_failures": 0,
      "i2c_failure_threshold": 10,
      "mpu6050_odr_hz": 500,
      "mpu6050_range_g": 16,
      "mpu6050_dlpf_hz": 44
    },
    "system_resources": {
      "free_heap_bytes": 214528,
      "min_free_heap_bytes": 198400,
      "core0_task": "SensorTask",
      "core1_task": "InferenceTask"
    }
  }
}
```

---

## 4. Subscription Strategy (Backend & Consumer Ingestion)

### 4.1 Enterprise Ingestion Pipeline

- **Pattern:** `v1/#`
- **Subscribed QoS:** 1
- **Operational Behavior:** Ingests all telemetry, alerts, and operational messages. Effective delivery QoS follows the MQTT specification rule `min(published_qos, subscribed_qos)`:
  - Telemetry published at QoS 0 is delivered to consumers at QoS 0.
  - Alerts published at QoS 1 are delivered to consumers at QoS 1 with mandatory acknowledgment (`PUBACK`).
- **High-Availability Scalability:** In production clusters (such as EMQX, HiveMQ, or Mosquitto 2.0+), consumers should subscribe via **MQTT v5.0 Shared Subscriptions** (`$share/backend_ingest_group/v1/#`) to load balance messages across multiple backend worker nodes and eliminate single-consumer bottlenecks.

### 4.2 Real-Time Monitoring & Alert Dashboards

- **Live Alert Feed:**
  - **Pattern:** `v1/+/+/+/+/alert`
  - **Subscribed QoS:** 1
  - **Purpose:** Feeds the **Alarm Audit Log** and triggers audio-visual alarms across all production lines with guaranteed at-least-once delivery.
- **Selected Machine Live Telemetry Feed:**
  - **Pattern:** `v1/{plant_id}/{line_id}/{machine_id}/accelerometer/telemetry`
  - **Subscribed QoS:** 0
  - **Purpose:** Powers active operator dashboard widgets (System State Banner, Instantaneous MSE, Anomaly Window Gauge, Latency breakdown parameters & chart, and 257-bin FFT spectrum) for the currently inspected asset without flooding the web client with fleet-wide telemetry.

### 4.3 Fleet Health & Connectivity Monitoring

- **Pattern:** `v1/+/+/+/+/heartbeat`
- **Subscribed QoS:** 0
- **Purpose:** Monitors edge node availability, sensor I2C communication failures (`consecutiveI2CFailures`), and active calibration states across the plant.

---

## 5. Security & Access Control (IEC 62443)

In accordance with IEC 62443-4-2 (Technical security requirements for IACS components) and IEC 62443-3-3 (System security requirements):

- **Transport Encryption:** Mandatory MQTTS over Port 8883 using TLS 1.2 or TLS 1.3 with forward-secret cipher suites (ECDHE-ECDSA/RSA-AES-GCM/CHACHA20). Plaintext MQTT over Port 1883 is disabled.
- **Client Authentication:** Mutual TLS (mTLS) with device-specific X.509 client certificates provisioned via secure hardware elements (TPM / ATECC608), or per-device unique cryptographic credentials.
- **Topic Authorization (Access Control Lists - ACL):**
  - Edge devices are strictly confined by broker ACL rules to publish only to their designated topic prefix (`v1/{assigned_plant}/{assigned_line}/{assigned_machine}/...`).
  - Edge devices are denied wildcard subscription privileges (`#` or `+`) to prevent unauthorized lateral visibility across neighboring production assets.
  - Administrative control topics (`v1/.../control` or `$SYS/#`) require elevated backend service credentials.
