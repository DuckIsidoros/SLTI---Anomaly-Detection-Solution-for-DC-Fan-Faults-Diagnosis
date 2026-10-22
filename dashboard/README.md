# Edge Condition Monitoring Dashboard (FastAPI + React / Vite)

Production-grade, minimal end-to-end industrial dashboard for the TinyML Anomaly Detection pipeline, conforming to **ISO 13374** (Condition Monitoring & Diagnostics) and **IEC 62443** (Zero-Trust MQTT Access Control).

---

## Architecture Overview

```text
dashboard/
├── backend/
│   ├── main.py              # FastAPI server, Mosquitto MQTT subscriber (v1/#), WebSocket stream
│   └── requirements.txt     # Backend dependencies (fastapi, uvicorn, paho-mqtt, websockets)
├── frontend/
│   ├── src/                 # React 18 + Vite components (MSE Gauge, 128-bin FFT spectrum, Latency, Alerts)
│   └── dist/                # Pre-built production SPA bundle served by FastAPI
├── mosquitto/               # Dockerized Eclipse Mosquitto MQTTS broker (Port 8883 / TLS 1.3)
├── mqtt_hierarchy.md        # Standard topic hierarchy & schema specification
└── run_dashboard.py         # One-command backend launcher
```

---

## Quickstart

### 1. Launch the FastAPI Backend

From the repository root:

```bash
python dashboard/run_dashboard.py
```

- **Dashboard UI**: [http://localhost:8000](http://localhost:8000)
- **Interactive Swagger API Docs**: [http://localhost:8000/docs](http://localhost:8000/docs)
- **Live WebSocket Stream**: `ws://localhost:8000/ws`

_Note: The dashboard stays in standby mode awaiting edge device connection. Live telemetry, FFT spectrum, and anomaly gauges activate automatically once an edge device (e.g. ESP32 or simulate_edge_node.py) connects and streams data._

---

### 2. Frontend Development (Hot-Reloading with Vite)

To develop or customize the frontend live:

```bash
cd dashboard/frontend
npm run dev
```

Open [http://localhost:5173](http://localhost:5173). Requests to `/api` and `/ws` are automatically proxied to the FastAPI backend at port 8000.

---

### 3. Rebuilding the Frontend Bundle

When modifying the React source code:

```bash
cd dashboard/frontend
npm run build
```

Builds the optimized single-page bundle to `dashboard/frontend/dist`, which FastAPI serves directly on `http://localhost:8000/`.

---

## Live Monitoring Widgets

1. **Reconstruction MSE & Anomaly Gauge**:
   - Real-time Mean Squared Error compared to empirical 99th-percentile dynamic threshold.
   - 20-window sliding ring buffer ($10/20$ cumulative anomaly decision filter).
2. **128-Bin FFT Amplitude Spectrum**:
   - High-speed SVG rendering across X, Y, and Z axes covering $10.74\text{ Hz}$ to $134.77\text{ Hz}$.
   - Axis filtering (All, X, Y, Z) and frequency scale indicators.
3. **Latched Alarm Incident Banner**:
   - Real-time pulse alert triggered on threshold trip ($10/20$ windows).
   - Displays peak MSE, empirical detection delay ($\text{ms}$), and the 16-dimensional bottleneck representation vector (`latent_vector`).
4. **Execution Latency Breakdown**:
   - Microsecond timing metrics for DSP preprocessing, TFLM INT8 inference, and end-to-end turnaround.
5. **Hardware Health & Fleet Diagnostics**:
   - Free internal SRAM heap, device uptime, MPU-6050 $\text{I}^2\text{C}$ bus drop counter, and active FreeRTOS task status.
6. **Incident Alert Audit Log**:
   - Chronological audit ledger recording anomalous events with full metrics.
