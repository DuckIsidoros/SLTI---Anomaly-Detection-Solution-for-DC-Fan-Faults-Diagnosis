# MQTT topic hierarchy architecture

## Introduction to MQTT design:
Our project follows ISA-95/UNS standard for managing data flows. We come up with design topics following this general rule:
Enterprise / Site / Area / Line / WorkCell / [Function]

## MQTT topics design:
In this project, we implement these 4 topics:
- factory/plant_01/workshop_a/line_02/pump_01/edge_node/fft (QoS: 0)
  - Since FFT data is pushed to the MQTT continuously and has big size, QoS 0 is proposed to avoid bottleneck and crash due to "ran out of RAM".
- factory/plant_01/workshop_a/line_02/pump_01/edge_node/inference (QoS: 1)
  - Since we want to ensure that no anomaly alarms are skipped, QoS 1 is needed.
- factory/plant_01/workshop_a/line_02/pump_01/edge_node/state (QoS: 1)
  - We combine QoS 1 and "Last Will and Testament" theory to identify the state (operating/sleeping/dying) of MCUs following the principle "single source of truth".
- factory/plant_01/workshop_a/line_02/pump_01/edge_node/cmd (QoS: 1)
  - This is necessary for configuring environment since we use 1D CNN Conditional Autoencoder algorithm.
  - More specifically, these are the features of cmd:
    - When the operators close the dashboard, the stream FFT will be closed and only AI inference will run.
    - It could modify the speed of refresh.
    - Reset/Reboot hardware without physical interation.
    - Further features is being researched.
 
## JSON Payload:
factory/plant_01/workshop_a/line_02/pump_01/edge_node/state
- Payload when the edge node is online:
{
  "timestamp": 1727702400000,
  "status": "ONLINE",
  "free_heap_bytes": 142320
}
- Payload when the edge node if offline:
{
  "timestamp": 1727702400000,
  "status": "OFFLINE",
  "reason": "connection_lost"
}

factory/plant_01/workshop_a/line_02/pump_01/edge_node/fft
{
  "timestamp": 1727702400123,
  "condition": 2,
  "spectrum": [...]
}

factory/plant_01/workshop_a/line_02/pump_01/edge_node/inference
{
  "timestamp": 1727702400123,
  "condition": 2,
  "loss": 0.0184,
  "threshold": 0.0450,
  "anomaly": false,
  "latency_ms": 14.2
}

factory/plant_01/workshop_a/line_02/pump_01/edge_node/cmd
{
  "timestamp": 1727702400000,
  "action": "SET_CONFIG",
  "params": {
    "condition": 2,
    "stream_fft": true,
    "fft_interval_ms": 200,
    "threshold_override": 0.0480
  }
}
