import asyncio
import json
import logging
import os
import ssl
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
    "heartbeat": None,
    "latest_telemetry": None,
    "telemetry_history": [],  # rolling recent 60 points for timeline charts
    "alerts": [],             # audit log of alerts
    "alarm_active": False,
}

connected_websockets: List[WebSocket] = []
loop: Optional[asyncio.AbstractEventLoop] = None
EDGE_TIMEOUT_SECONDS = 5.0

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
        if "device_id" in payload:
            state["device_status"]["device_id"] = payload["device_id"]

        if msg_type == "status":
            dev_status = payload.get("status", "ONLINE")
            state["device_status"]["status"] = dev_status
            schedule_broadcast("status", payload)

        elif msg_type == "telemetry":
            if state["device_status"]["status"] != "ONLINE":
                state["device_status"]["status"] = "ONLINE"
                schedule_broadcast("status", {"status": "ONLINE", "device_id": state["device_status"]["device_id"]})

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
            if state["device_status"]["status"] != "ONLINE":
                state["device_status"]["status"] = "ONLINE"
                schedule_broadcast("status", {"status": "ONLINE", "device_id": state["device_status"]["device_id"]})
            state["heartbeat"] = payload.get("payload", {})
            schedule_broadcast("heartbeat", payload)

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
        logger.warning(f"Could not connect to MQTT broker ({e}). Waiting for MQTT broker and edge node...")

# ------------------------------------------------------------------------------
# Edge Node Liveness Watchdog
# ------------------------------------------------------------------------------
async def edge_liveness_watchdog():
    """Periodically checks if the edge node has stopped transmitting telemetry."""
    while True:
        await asyncio.sleep(1.0)
        if state["device_status"]["status"] == "ONLINE":
            last_seen = state["device_status"].get("last_seen")
            if last_seen and (time.time() - last_seen > EDGE_TIMEOUT_SECONDS):
                logger.info(f"Edge device timed out (> {EDGE_TIMEOUT_SECONDS}s inactive). Marking OFFLINE.")
                state["device_status"]["status"] = "OFFLINE"
                schedule_broadcast("status", {
                    "status": "OFFLINE",
                    "device_id": state["device_status"]["device_id"],
                    "reason": "timeout"
                })

# ------------------------------------------------------------------------------
# Lifecycle & Endpoints
# ------------------------------------------------------------------------------
@app.on_event("startup")
async def startup_event():
    global loop
    loop = asyncio.get_running_loop()
    start_mqtt_client()
    asyncio.create_task(edge_liveness_watchdog())

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
