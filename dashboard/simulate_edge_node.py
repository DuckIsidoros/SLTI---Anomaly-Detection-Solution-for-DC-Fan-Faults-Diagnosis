#!/usr/bin/env python3
"""
Simulate ESP32 Edge Device Publishing to Mosquitto MQTT Broker
Conforms strictly to dashboard/mqtt_hierarchy.md and dashboard/mosquitto ACL:
- Authenticates as 'edge_node_01' / 'EdgePass2026!'
- Publishes to:
  * v1/plant_hcm/line_01/cnc_05/accelerometer/telemetry (QoS 0) - 128-bin FFT & MSE
  * v1/plant_hcm/line_01/cnc_05/accelerometer/alert     (QoS 1) - Latched alarm + 16D latent vector
  * v1/plant_hcm/line_01/cnc_05/accelerometer/heartbeat (QoS 0) - Periodic device health
  * v1/plant_hcm/line_01/cnc_05/status                  (QoS 1) - Birth & LWT status
"""

import json
import math
import os
import random
import ssl
import sys
import time
import paho.mqtt.client as mqtt

# ==============================================================================
# Configuration
# ==============================================================================
BROKER_HOST = os.getenv("MQTT_HOST", "localhost")
BROKER_PORT = int(os.getenv("MQTT_PORT", "8883"))
USERNAME = os.getenv("MQTT_USER", "layvoduc")
PASSWORD = os.getenv("MQTT_PASS", "0383969099")

DEVICE_ID = "esp32_edge_01"
BASE_TOPIC = "v1/plant_hcm/line_01/cnc_05"
TOPIC_TELEMETRY = f"{BASE_TOPIC}/accelerometer/telemetry"
TOPIC_ALERT = f"{BASE_TOPIC}/accelerometer/alert"
TOPIC_HEARTBEAT = f"{BASE_TOPIC}/accelerometer/heartbeat"
TOPIC_STATUS = f"{BASE_TOPIC}/status"

CA_CERT_PATH = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "mosquitto", "certs", "ca.crt")
)

def get_iso8601_timestamp() -> str:
    now = time.time()
    ms = int((now - int(now)) * 1000)
    return time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(now)) + f".{ms:03d}Z"

def generate_128bin_fft(is_fault: bool):
    """
    Generates 128 frequency bins (bins 11 to 138, 10.74 Hz to 134.77 Hz).
    Simulates DC motor vibration profile with harmonic resonance during fault.
    """
    x_axis, y_axis, z_axis = [], [], []
    for b in range(11, 139):
        # Fan fundamental peak around bin 30 (~29.3 Hz)
        p1 = math.exp(-((b - 30) ** 2) / 10.0) * (0.42 if is_fault else 0.12)
        # Bearing / blade obstruction peak around bin 72 (~70.3 Hz)
        p2 = math.exp(-((b - 72) ** 2) / 14.0) * (0.35 if is_fault else 0.04)
        noise = random.uniform(0.005, 0.018)

        x_axis.append(round(p1 * 0.8 + noise, 4))
        y_axis.append(round(p2 * 0.7 + noise * 0.8, 4))
        z_axis.append(round(p1 + p2 * 0.9 + noise * 1.2, 4))

    return {
        "unit": "g",
        "window_size": 512,
        "sampling_rate_hz": 500,
        "delta_f_hz": 0.9765625,
        "bin_count": 128,
        "freq_start_hz": 10.74,
        "freq_end_hz": 134.77,
        "axes": {"x": x_axis, "y": y_axis, "z": z_axis}
    }

def main():
    print("=" * 70)
    print("  ESP32 EDGE INFERENCE SIMULATOR (MQTT PUBLISHER)")
    print("=" * 70)
    print(f"Target Broker: {BROKER_HOST}:{BROKER_PORT}")
    print(f"Username     : {USERNAME}")
    print(f"Topic Base   : {BASE_TOPIC}")
    print("=" * 70)

    try:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=DEVICE_ID, protocol=mqtt.MQTTv311)
    except AttributeError:
        client = mqtt.Client(client_id=DEVICE_ID, protocol=mqtt.MQTTv311)
    client.username_pw_set(USERNAME, PASSWORD)

    # Configure LWT (Last Will and Testament)
    lwt_payload = json.dumps({
        "device_id": DEVICE_ID,
        "timestamp": get_iso8601_timestamp(),
        "msg_type": "status",
        "status": "OFFLINE",
        "reason": "connection_lost"
    })
    client.will_set(TOPIC_STATUS, lwt_payload, qos=1, retain=True)

    # Configure TLS if on port 8883
    if BROKER_PORT == 8883:
        if os.path.exists(CA_CERT_PATH):
            client.tls_set(ca_certs=CA_CERT_PATH, tls_version=ssl.PROTOCOL_TLS_CLIENT)
            client.tls_insecure_set(True)
            print(f"[TLS] Using CA certificate: {CA_CERT_PATH}")
        else:
            print(f"[TLS] Warning: CA cert not found at {CA_CERT_PATH}. Ensure broker is reachable.")

    try:
        client.connect(BROKER_HOST, BROKER_PORT, keepalive=60)
        client.loop_start()
    except Exception as e:
        print(f"[ERROR] Failed to connect to MQTT broker: {e}")
        print("Tip: If Mosquitto Docker is not running, run:")
        print("  cd dashboard/mosquitto && docker compose up -d")
        sys.exit(1)

    print("[MQTT] Connected successfully!")

    # Publish Birth Status (Online)
    birth_payload = json.dumps({
        "device_id": DEVICE_ID,
        "timestamp": get_iso8601_timestamp(),
        "msg_type": "status",
        "status": "ONLINE"
    })
    client.publish(TOPIC_STATUS, birth_payload, qos=1, retain=True)
    print(f"[MQTT] Published birth status to {TOPIC_STATUS}")

    step = 0
    anomaly_ring_count = 0
    alarm_active = False
    last_heartbeat = 0
    dynamic_threshold = 0.00785

    print("\nStreaming 512ms telemetry windows... (Press Ctrl+C to stop)")
    print("Pattern: Normal (20 windows) -> Anomaly Burst (15 windows) -> Recovery\n")

    try:
        while True:
            step += 1
            now = time.time()
            now_iso = get_iso8601_timestamp()

            # Create an anomaly cycle: windows 25..38 trigger a fault
            cycle_step = step % 45
            is_fault = (25 <= cycle_step <= 38)

            if is_fault:
                base_mse = 0.0165 + random.uniform(-0.001, 0.003)
                anomaly_ring_count = min(20, anomaly_ring_count + 1)
            else:
                base_mse = 0.0034 + random.uniform(-0.0004, 0.0004)
                anomaly_ring_count = max(0, anomaly_ring_count - 1)

            is_window_anomaly = (base_mse >= dynamic_threshold)

            # Execution Latencies in microseconds
            dsp_us = random.randint(1230, 1260)
            inference_us = random.randint(4840, 4880)
            e2e_us = dsp_us + inference_us + random.randint(680, 720)

            # 1. Telemetry Payload (QoS 0)
            telemetry_payload = {
                "device_id": DEVICE_ID,
                "timestamp": now_iso,
                "msg_type": "telemetry",
                "payload": {
                    "system_state": "MONITORING",
                    "environment": "ENV_TABLE",
                    "calibration": {
                        "is_calibrating": False,
                        "step": 100,
                        "total_steps": 100
                    },
                    "inference": {
                        "current_mse": round(base_mse, 6),
                        "dynamic_threshold": dynamic_threshold,
                        "is_window_anomaly": is_window_anomaly
                    },
                    "anomaly_ring": {
                        "anomaly_count": anomaly_ring_count,
                        "window_span": 20,
                        "trigger_threshold": 10
                    },
                    "latency_us": {
                        "dsp": dsp_us,
                        "inference": inference_us,
                        "e2e": e2e_us
                    },
                    "fft": generate_128bin_fft(is_fault)
                }
            }
            client.publish(TOPIC_TELEMETRY, json.dumps(telemetry_payload), qos=0)

            # 2. Alert Trigger Logic (QoS 1)
            threshold_reached = (anomaly_ring_count >= 10)
            if threshold_reached and not alarm_active:
                alarm_active = True
                alert_payload = {
                    "device_id": DEVICE_ID,
                    "timestamp": now_iso,
                    "msg_type": "alert",
                    "payload": {
                        "event": "ALARM_LATCHED",
                        "alarm_active": True,
                        "alarm_latch_ms": 3000,
                        "incident_metrics": {
                            "current_mse": round(base_mse, 6),
                            "peak_mse": round(base_mse * 1.18, 6),
                            "dynamic_threshold": dynamic_threshold,
                            "anomaly_count": anomaly_ring_count,
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
                            "latent_vector": [-18, 42, -5, 12, 0, -33, 89, -14, 27, -8, 64, -2, 19, -45, 11, -7]
                        },
                        "latency_us": {
                            "dsp": dsp_us,
                            "inference": inference_us,
                            "e2e": e2e_us
                        }
                    }
                }
                client.publish(TOPIC_ALERT, json.dumps(alert_payload), qos=1)
                print(f"\n[>>> ALERT PUBLISHED <<<] Peak MSE: {base_mse:.6f} | Anomaly Count: {anomaly_ring_count}/20")
            elif not threshold_reached and alarm_active:
                alarm_active = False
                print("\n[ALARM CLEARED] Operating condition restored to baseline.")

            # 3. Heartbeat Payload (every 30 seconds, QoS 0)
            if now - last_heartbeat >= 30:
                last_heartbeat = now
                heartbeat_payload = {
                    "device_id": DEVICE_ID,
                    "timestamp": now_iso,
                    "msg_type": "heartbeat",
                    "payload": {
                        "system_state": "MONITORING",
                        "environment": "ENV_TABLE",
                        "uptime_seconds": step * 0.512,
                        "calibration": {
                            "is_calibrated": True,
                            "dynamic_threshold": dynamic_threshold,
                            "calibration_windows": 100
                        },
                        "hardware_health": {
                            "i2c_consecutive_failures": 0,
                            "i2c_failure_threshold": 10,
                            "mpu6050_odr_hz": 500,
                            "mpu6050_range_g": 16,
                            "mpu6050_dlpf_hz": 44
                        },
                        "system_resources": {
                            "free_heap_bytes": 214528 - random.randint(0, 1024),
                            "min_free_heap_bytes": 198400,
                            "core0_task": "SensorTask",
                            "core1_task": "InferenceTask"
                        }
                    }
                }
                client.publish(TOPIC_HEARTBEAT, json.dumps(heartbeat_payload), qos=0)
                print("[HEARTBEAT] Health packet published.")

            status_str = f"Window #{step:03d} | MSE: {base_mse:.6f} (Thresh: {dynamic_threshold}) | Ring: {anomaly_ring_count:02d}/20 | Lat: {e2e_us}us"
            print(f"\r{status_str}", end="", flush=True)

            # Wait 512ms (sampling stride: 256 samples / 500Hz)
            time.sleep(0.512)

    except KeyboardInterrupt:
        print("\n\nStopping simulator...")
        client.publish(TOPIC_STATUS, lwt_payload, qos=1, retain=True)
        time.sleep(0.5)
        client.loop_stop()
        client.disconnect()
        print("Disconnected cleanly.")

if __name__ == "__main__":
    main()
