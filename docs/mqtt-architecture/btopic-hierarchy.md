# MQTT Topic Hierarchy Architecture

## 1. Topic Structure
Tuân thủ ISO 13374 để phân cấp dữ liệu Condition Monitoring.
- **Format:** `v1/{plant_id}/{line_id}/{machine_id}/{sensor_type}/{msg_type}`
- **Ví dụ:** `v1/plant_hcm/line_01/cnc_05/accelerometer/telemetry`

## 2. Message Types & QoS
| msg_type | Mô tả | QoS | Lý do |
|----------|-------|-----|-------|
| `telemetry` | Dữ liệu cảm biến tần số cao | 0 | Chấp nhận mất gói để giảm băng thông/độ trễ |
| `alert` | Kết quả suy luận AI Edge (lỗi máy) | 1 | Bắt buộc không được mất dữ liệu cảnh báo |
| `heartbeat` | Trạng thái thiết bị (pin, online) | 0 | Dữ liệu định kỳ, không quan trọng |

## 3. Standard Payload Schema (JSON)
Sử dụng JSON. Timestamp bắt buộc chuẩn ISO 8601.

### Telemetry Payload
```json
{
  "device_id": "edge_node_01",
  "timestamp": "2026-08-22T10:00:00.123Z",
  "msg_type": "telemetry",
  "payload": {
    "vibration": { "x_rms": 2.45, "y_rms": 1.89, "z_rms": 3.10 }
  }
}

Alert Payload

{
  "device_id": "edge_node_01",
  "timestamp": "2026-08-22T10:05:00.456Z",
  "msg_type": "alert",
  "payload": {
    "edge_inference": { "anomaly_score": 0.87, "fault_type": "bearing_fault", "confidence": 0.92 }
  }
}

4. Subscription Strategy (Backend)
Backend Consumer: Subscribe v1/# (QoS 1) để hứng toàn bộ data.
Dashboard: Subscribe v1/+/+/+/+/alert (QoS 1) chỉ để hiện cảnh báo.
5. Security (IEC 62443)
Bắt buộc dùng MQTTS (Port 8883) với TLS.
Device chỉ được publish vào topic của chính nó.
