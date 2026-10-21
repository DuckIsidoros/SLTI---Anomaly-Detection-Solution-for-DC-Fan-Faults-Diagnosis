import asyncio
import json
import logging
import os
import ssl
import threading
import time
from typing import Dict, List, Any, Optional

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
import paho.mqtt.client as mqtt

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("dashboard_backend")

app = FastAPI(title="Edge Condition Monitoring Dashboard API", version="1.0.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# ------------------------------------------------------------------------------
# Global In-Memory State
# ------------------------------------------------------------------------------
state: Dict[str, Any] = {
    "device_status": {
        "status": "OFFLINE",
        "device_id": "esp32_edge_01",
        "last_seen": None,
        "mqtt_connected": False,
    },
    "heartbeat": {
        "system_state": "STANDBY",
        "environment": "ENV_TABLE",
        "uptime_seconds": 0,
        "calibration": {"is_calibrated": False, "dynamic_threshold": 0.00785, "calibration_windows": 100},
        "hardware_health": {"i2c_consecutive_failures": 0, "i2c_failure_threshold": 10, "mpu6050_odr_hz": 500, "mpu6050_range_g": 16, "mpu6050_dlpf_hz": 44},
        "system_resources": {"free_heap_bytes": 214528, "min_free_heap_bytes": 198400, "core0_task": "SensorTask", "core1_task": "InferenceTask"}
    },
    "latest_telemetry": None,
    "telemetry_history": [],  # rolling recent 60 points for timeline charts
    "alerts": [],             # audit log of alerts
    "alarm_active": False,
}

connected_websockets: List[WebSocket] = []
loop: Optional[asyncio.AbstractEventLoop] = None

async def broadcast_ws(event_type: str, data: Any):
    if not connected_websockets:
        return
    message = json.dumps({"type": event_type, "data": data})
    disconnected = []
    for ws in connected_websockets:
        try:
            await ws.send_text(message)
        except Exception:
            disconnected.append(ws)
    for ws in disconnected:
        if ws in connected_websockets:
            connected_websockets.remove(ws)

def schedule_broadcast(event_type: str, data: Any):
    global loop
    if loop and loop.is_running():
        asyncio.run_coroutine_threadsafe(broadcast_ws(event_type, data), loop)

# ------------------------------------------------------------------------------
# MQTT Ingestion Engine (ISO 13374 / IEC 62443 backend_service)
# ------------------------------------------------------------------------------
MQTT_BROKER_HOST = os.getenv("MQTT_HOST", "localhost")
MQTT_BROKER_PORT = int(os.getenv("MQTT_PORT", "8883"))
MQTT_USERNAME = os.getenv("MQTT_USER", "backend_service")
MQTT_PASSWORD = os.getenv("MQTT_PASS", "BackendPass2026!")

# Path to root CA certificate
CA_CERT_PATH = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "mosquitto", "certs", "ca.crt")
)

def on_mqtt_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        logger.info(f"Connected to Mosquitto broker at {MQTT_BROKER_HOST}:{MQTT_BROKER_PORT}")
        state["device_status"]["mqtt_connected"] = True
        client.subscribe("v1/#", qos=1)
        schedule_broadcast("broker_status", {"connected": True})
    else:
        logger.warning(f"MQTT connection returned code {rc}")
        state["device_status"]["mqtt_connected"] = False

def on_mqtt_disconnect(client, userdata, rc, properties=None):
    logger.warning("Disconnected from MQTT broker.")
    state["device_status"]["mqtt_connected"] = False
    schedule_broadcast("broker_status", {"connected": False})

def on_mqtt_message(client, userdata, msg):
    try:
        topic = msg.topic
        payload = json.loads(msg.payload.decode("utf-8"))
        logger.debug(f"MQTT message on {topic}")

        # Topic format: v1/{plant_id}/{line_id}/{machine_id}/{sensor_type}/{msg_type}
        parts = topic.split("/")
        msg_type = parts[-1] if len(parts) >= 2 else ""

        state["device_status"]["last_seen"] = time.time()
        state["device_status"]["device_id"] = payload.get("device_id", "esp32_edge_01")

        if msg_type == "telemetry":
            state["latest_telemetry"] = payload
            hist_entry = {
                "timestamp": payload.get("timestamp"),
                "mse": payload.get("payload", {}).get("inference", {}).get("current_mse", 0.0),
                "threshold": payload.get("payload", {}).get("inference", {}).get("dynamic_threshold", 0.0),
                "is_anomaly": payload.get("payload", {}).get("inference", {}).get("is_window_anomaly", False),
                "anomaly_count": payload.get("payload", {}).get("anomaly_ring", {}).get("anomaly_count", 0),
                "latency_us": payload.get("payload", {}).get("latency_us", {}),
            }
            state["telemetry_history"].append(hist_entry)
            if len(state["telemetry_history"]) > 100:
                state["telemetry_history"].pop(0)

            schedule_broadcast("telemetry", payload)

        elif msg_type == "alert":
            state["alarm_active"] = True
            state["alerts"].insert(0, payload)
            if len(state["alerts"]) > 50:
                state["alerts"].pop()
            schedule_broadcast("alert", payload)

        elif msg_type == "heartbeat":
            state["heartbeat"] = payload.get("payload", {})
            schedule_broadcast("heartbeat", payload)

        elif msg_type == "status":
            dev_status = payload.get("status", "ONLINE")
            state["device_status"]["status"] = dev_status
            schedule_broadcast("status", payload)

    except Exception as e:
        logger.error(f"Error handling MQTT message on {msg.topic}: {e}")

def start_mqtt_client():
    try:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="fastapi_backend_consumer", protocol=mqtt.MQTTv311)
    except AttributeError:
        client = mqtt.Client(client_id="fastapi_backend_consumer", protocol=mqtt.MQTTv311)
    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    client.on_connect = on_mqtt_connect
    client.on_disconnect = on_mqtt_disconnect
    client.on_message = on_mqtt_message

    if MQTT_BROKER_PORT == 8883:
        if os.path.exists(CA_CERT_PATH):
            client.tls_set(ca_certs=CA_CERT_PATH, tls_version=ssl.PROTOCOL_TLS_CLIENT)
            client.tls_insecure_set(True)  # allows testing with localhost self-signed certs
            logger.info(f"Loaded TLS certificate from {CA_CERT_PATH}")
        else:
            logger.warning(f"CA certificate not found at {CA_CERT_PATH}, attempting unencrypted fallback if available")

    try:
        client.connect_async(MQTT_BROKER_HOST, MQTT_BROKER_PORT, 60)
        client.loop_start()
    except Exception as e:
        logger.warning(f"Could not connect to MQTT broker ({e}). Simulation mode will run concurrently.")

# ------------------------------------------------------------------------------
# Demo Simulator: Generates realistic 128-bin FFT and telemetry if broker is quiet
# ------------------------------------------------------------------------------
def simulator_thread():
    import math
    import random
    step = 0
    while True:
        time.sleep(0.512) # 512ms hop size matching 50% stride at 500Hz
        # If no real telemetry arrived in the last 3 seconds, generate simulated pipeline stream
        if not state["device_status"]["last_seen"] or (time.time() - state["device_status"]["last_seen"] > 3.0):
            step += 1
            now_iso = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime()) + f".{int(time.time()*1000)%1000:03d}Z"
            state["device_status"]["status"] = "ONLINE"
            state["device_status"]["device_id"] = "esp32_edge_01 (Simulated)"

            # Simulate baseline vibration + occasional resonant fault
            is_fault = (step % 40 >= 30)
            base_mse = 0.0032 + random.uniform(-0.0004, 0.0005)
            mse = (base_mse * 4.5 if is_fault else base_mse)
            threshold = 0.00785
            is_anomaly = (mse >= threshold)

            # Generate 128-bin FFT spectrum (bins 11 to 138, 10.74 Hz to 134.77 Hz)
            # Characteristic motor peak around bin 30 (~29.3 Hz DC Fan fundamental)
            x_spectrum = []
            y_spectrum = []
            z_spectrum = []
            for b in range(11, 139):
                freq = b * (500.0 / 512.0)
                peak_1 = math.exp(-((b - 30) ** 2) / 8.0) * (0.45 if is_fault else 0.12)
                peak_2 = math.exp(-((b - 60) ** 2) / 12.0) * (0.28 if is_fault else 0.05)
                noise = random.uniform(0.005, 0.02)
                x_val = round(peak_1 + noise, 4)
                y_val = round(peak_2 + noise * 0.8, 4)
                z_val = round(peak_1 * 0.7 + peak_2 * 0.5 + noise * 1.2, 4)
                x_spectrum.append(x_val)
                y_spectrum.append(y_val)
                z_spectrum.append(z_val)

            sim_telemetry = {
                "device_id": "esp32_edge_01",
                "timestamp": now_iso,
                "msg_type": "telemetry",
                "payload": {
                    "system_state": "MONITORING",
                    "environment": "ENV_TABLE",
                    "calibration": {"is_calibrating": False, "step": 100, "total_steps": 100},
                    "inference": {"current_mse": round(mse, 6), "dynamic_threshold": threshold, "is_window_anomaly": is_anomaly},
                    "anomaly_ring": {"anomaly_count": (12 if is_fault else (2 if is_anomaly else 0)), "window_span": 20, "trigger_threshold": 10},
                    "latency_us": {"dsp": random.randint(1220, 1260), "inference": random.randint(4820, 4890), "e2e": random.randint(6780, 6860)},
                    "fft": {
                        "unit": "g",
                        "window_size": 512,
                        "sampling_rate_hz": 500,
                        "delta_f_hz": 0.9765625,
                        "bin_count": 128,
                        "freq_start_hz": 10.74,
                        "freq_end_hz": 134.77,
                        "axes": {"x": x_spectrum, "y": y_spectrum, "z": z_spectrum}
                    }
                }
            }
            state["latest_telemetry"] = sim_telemetry
            hist_entry = {
                "timestamp": now_iso,
                "mse": round(mse, 6),
                "threshold": threshold,
                "is_anomaly": is_anomaly,
                "anomaly_count": sim_telemetry["payload"]["anomaly_ring"]["anomaly_count"],
                "latency_us": sim_telemetry["payload"]["latency_us"]
            }
            state["telemetry_history"].append(hist_entry)
            if len(state["telemetry_history"]) > 100:
                state["telemetry_history"].pop(0)

            schedule_broadcast("telemetry", sim_telemetry)

            # Trigger alert on fault entry
            if is_fault and (step % 40 == 30):
                state["alarm_active"] = True
                sim_alert = {
                    "device_id": "esp32_edge_01",
                    "timestamp": now_iso,
                    "msg_type": "alert",
                    "payload": {
                        "event": "ALARM_LATCHED",
                        "alarm_active": True,
                        "alarm_latch_ms": 3000,
                        "incident_metrics": {
                            "current_mse": round(mse, 6),
                            "peak_mse": round(mse * 1.15, 6),
                            "dynamic_threshold": threshold,
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
                            "latent_vector": [-18, 42, -5, 12, 0, -33, 89, -14, 27, -8, 64, -2, 19, -45, 11, -7]
                        },
                        "latency_us": sim_telemetry["payload"]["latency_us"]
                    }
                }
                state["alerts"].insert(0, sim_alert)
                if len(state["alerts"]) > 50:
                    state["alerts"].pop()
                schedule_broadcast("alert", sim_alert)

# ------------------------------------------------------------------------------
# Lifecycle & Endpoints
# ------------------------------------------------------------------------------
@app.on_event("startup")
async def startup_event():
    global loop
    loop = asyncio.get_running_loop()
    start_mqtt_client()
    sim_t = threading.Thread(target=simulator_thread, daemon=True)
    sim_t.start()

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    connected_websockets.append(websocket)
    # Send initial state snapshot
    await websocket.send_text(json.dumps({
        "type": "initial_state",
        "data": {
            "device_status": state["device_status"],
            "heartbeat": state["heartbeat"],
            "latest_telemetry": state["latest_telemetry"],
            "telemetry_history": state["telemetry_history"][-30:],
            "alerts": state["alerts"][:10],
            "alarm_active": state["alarm_active"]
        }
    }))
    try:
        while True:
            # Keep connection alive
            await websocket.receive_text()
    except WebSocketDisconnect:
        if websocket in connected_websockets:
            connected_websockets.remove(websocket)

@app.get("/api/status")
def get_status():
    return {
        "device_status": state["device_status"],
        "heartbeat": state["heartbeat"],
        "alarm_active": state["alarm_active"]
    }

@app.get("/api/telemetry")
def get_telemetry():
    return {
        "latest": state["latest_telemetry"],
        "history": state["telemetry_history"][-50:]
    }

@app.get("/api/alerts")
def get_alerts():
    return {"alerts": state["alerts"], "alarm_active": state["alarm_active"]}

@app.post("/api/alerts/clear")
def clear_alerts():
    state["alarm_active"] = False
    schedule_broadcast("alarm_cleared", {"alarm_active": False})
    return {"status": "cleared"}

# Static build fallback if frontend dist exists
frontend_dist = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "frontend", "dist"))
if os.path.exists(frontend_dist):
    app.mount("/assets", StaticFiles(directory=os.path.join(frontend_dist, "assets")), name="assets")
    @app.get("/{full_path:path}")
    def serve_frontend(full_path: str):
        index_file = os.path.join(frontend_dist, "index.html")
        return FileResponse(index_file)
