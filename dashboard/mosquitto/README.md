# Mosquitto MQTT Broker (Docker Setup)

This package contains everything required to deploy Eclipse Mosquitto with **MQTTS (Port 8883)** and **Secure WebSockets (Port 8084)**, configured with IEC 62443 topic ACLs and authentication.

---

## Folder Structure

```
mosquitto-docker/
├── docker-compose.yml       # Docker Compose service definition
├── setup.sh                 # Automated environment initialization and launch script
├── generate_certs.sh        # TLS/mTLS certificate generator with SANs
├── README.md                # Quickstart and testing instructions
├── config/
│   ├── mosquitto.conf       # Main Mosquitto configuration (ports, TLS, listeners)
│   ├── acl                  # Access Control List (IEC 62443 topic scoping)
│   └── passwd               # Hashed credentials file (SHA-512 crypt)
├── certs/                   # TLS certificates (ca.crt, server.crt, server.key, client.crt)
├── data/                    # Persistent MQTT database storage
└── log/                     # Mosquitto broker log files
```

---

## Quickstart

### 1. Launch the Broker
Run the automated setup script:
```bash
./setup.sh
```
Or directly with Docker Compose:
```bash
docker compose up -d
```

### 2. Check Broker Logs
```bash
docker compose logs -f
```

### 3. Stop the Broker
```bash
docker compose down
```

---

## Pre-Configured Accounts & Topic Permissions

| Username | Default Password | Role | Allowed Topics & Permissions |
|:---|:---|:---|:---|
| `edge_node_01` | `EdgePass2026!` | Edge Gateway | **Write-only** to `v1/plant_hcm/line_01/cnc_05/+/+` and `v1/plant_hcm/line_01/cnc_05/status` |
| `dashboard_user` | `DashPass2026!` | Web App | **Read-only** to `v1/+/+/+/+/alert` |
| `backend_service` | `BackendPass2026!` | Ingestion Engine | **Read/Write** to `v1/#` |

---

## Testing the Setup

### 1. Test Edge Device Publishing (Port 8883 - MQTTS)
Publish telemetry from an edge node:
```bash
mosquitto_pub -h localhost -p 8883 \
  --cafile certs/ca.crt \
  -u edge_node_01 -P 'EdgePass2026!' \
  -t "v1/plant_hcm/line_01/cnc_05/accelerometer/telemetry" \
  -m '{"device_id":"edge_node_01","timestamp":"2026-08-22T10:00:00.123Z","msg_type":"telemetry","payload":{"vibration":{"unit":"mm/s","x_rms":2.45,"y_rms":1.89,"z_rms":3.10}}}' \
  -q 0
```

### 2. Test Backend Consumer (Port 8883 - MQTTS)
Subscribe to all data across the plant:
```bash
mosquitto_sub -h localhost -p 8883 \
  --cafile certs/ca.crt \
  -u backend_service -P 'BackendPass2026!' \
  -t "v1/#" -q 1 -v
```

### 3. Test Web Dashboard via WebSockets (Port 8084 - WSS)
Connect using `mqtt.js` in your web frontend:
```javascript
import mqtt from "mqtt";

// Connect over Secure WebSockets (WSS)
const client = mqtt.connect("wss://localhost:8084/mqtt", {
  clientId: "web_dash_" + Math.random().toString(16).slice(2, 8),
  username: "dashboard_user",
  password: "DashPass2026!",
  clean: true,
  reconnectPeriod: 5000,
});

client.on("connect", () => {
  console.log("Connected to Mosquitto via WSS!");
  // Subscribe to real-time machine alerts
  client.subscribe("v1/+/+/+/+/alert", { qos: 1 });
});

client.on("message", (topic, message) => {
  console.log("Alert event on", topic, ":", JSON.parse(message.toString()));
});
```
