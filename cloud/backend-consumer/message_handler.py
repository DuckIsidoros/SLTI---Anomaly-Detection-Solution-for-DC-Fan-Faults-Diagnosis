"""
Message Handler Module for Backend Consumer Service.
Processes received MQTT messages and routes them to appropriate handlers.
"""

import json
import logging
from typing import Dict, Any

logger = logging.getLogger(__name__)


class MessageHandler:
    """
    Handles incoming MQTT messages based on message type.
    
    Why: Separates message routing logic from MQTT connection logic.
    This makes the system easier to test and maintain.
    """
    
    def handle_message(self, topic: str, payload_bytes: bytes):
        """
        Main entry point for message handling.
        
        Input:
            topic (str): MQTT topic string (e.g., "v1/plant_hcm/line_01/cnc_05/accelerometer/telemetry").
            payload_bytes (bytes): Raw message payload from MQTT broker.
        Output:
            None. Routes to appropriate handler based on msg_type.
        """
        try:
            # Decode bytes to string and parse JSON
            payload_str = payload_bytes.decode('utf-8')
            payload = json.loads(payload_str)
            
            # Extract message type for routing
            # Why: msg_type determines how we process the data (store, alert, or ignore)
            msg_type = payload.get('msg_type')
            
            if not msg_type:
                logger.warning("Message missing 'msg_type' field on topic: %s", topic)
                return
            
            # Route based on message type
            # Why: Different message types require different processing logic
            if msg_type == 'telemetry':
                self._handle_telemetry(topic, payload)
            elif msg_type == 'alert':
                self._handle_alert(topic, payload)
            elif msg_type == 'heartbeat':
                self._handle_heartbeat(topic, payload)
            else:
                logger.warning("Unknown message type: %s", msg_type)
                
        except json.JSONDecodeError as e:
            # Why: Invalid JSON means the sender has a bug or we have a schema mismatch
            logger.error("Failed to parse JSON on topic %s: %s", topic, str(e))
        except Exception as e:
            # Catch-all for unexpected errors to prevent service crash
            logger.error("Unexpected error handling message on topic %s: %s", topic, str(e))
    
    def _handle_telemetry(self, topic: str, payload: Dict[str, Any]):
        """
        Handle telemetry messages (sensor data).
        
        Input:
            topic (str): MQTT topic string.
            payload (dict): Parsed JSON payload containing sensor readings.
        Output:
            None. Stores data to Time-series DB.
        """
        device_id = payload.get('device_id')
        timestamp = payload.get('timestamp')
        
        # Why: Telemetry data is time-series data that needs to be stored for analysis
        # This is the main data source for ML model training and anomaly detection
        logger.info("Telemetry from device %s at %s", device_id, timestamp)
        
        # Extract vibration data for logging/debugging
        vibration_data = payload.get('payload', {}).get('vibration', {})
        if vibration_data:
            logger.debug("Vibration data: %s", vibration_data)
        
        # TO-DO: Implement Time-series DB write logic
        # Options:
        # 1. InfluxDB: Best for high-frequency sensor data
        # 2. TimescaleDB: PostgreSQL extension, good for relational + time-series
        # 3. Azure Data Explorer: If using Azure ecosystem
        # 
        # Example InfluxDB write:
        # self.influx_client.write(
        #     bucket="machine_health",
        #     record={
        #         "measurement": "vibration",
        #         "tags": {"device_id": device_id},
        #         "time": timestamp,
        #         "fields": vibration_data
        #     }
        # )
    
    def _handle_alert(self, topic: str, payload: Dict[str, Any]):
        """
        Handle alert messages (Edge AI inference results).
        
        Input:
            topic (str): MQTT topic string.
            payload (dict): Parsed JSON payload containing AI inference results.
        Output:
            None. Triggers alert notification system.
        """
        device_id = payload.get('device_id')
        edge_inference = payload.get('payload', {}).get('edge_inference', {})
        
        anomaly_score = edge_inference.get('anomaly_score')
        fault_type = edge_inference.get('fault_type')
        confidence = edge_inference.get('confidence')
        
        # Why: Alerts are critical - they indicate potential machine failure
        # Must be processed immediately and trigger notifications
        # This aligns with ISO 13374 Condition Monitoring standard
        
        logger.warning(
            "ALERT from device %s: fault=%s, score=%.2f, confidence=%.2f", 
            device_id, fault_type, anomaly_score, confidence
        )
        
        # TO-DO: Implement alert notification system
        # Options:
        # 1. WebSocket: Push to real-time dashboard
        # 2. Email: Send to maintenance team
        # 3. SMS: For critical alerts (high anomaly score)
        # 4. Database: Store in Relational DB for audit trail
        #
        # Example:
        # if anomaly_score > 0.8:
        #     self.notification_service.send_sms(
        #         phone="+84123456789",
        #         message=f"Critical alert: Machine {device_id} - {fault_type}"
        #     )
        # 
        # self.alert_db.insert({
        #     "device_id": device_id,
        #     "timestamp": payload.get('timestamp'),
        #     "fault_type": fault_type,
        #     "anomaly_score": anomaly_score
        # })
    
    def _handle_heartbeat(self, topic: str, payload: Dict[str, Any]):
        """
        Handle heartbeat messages (device status).
        
        Input:
            topic (str): MQTT topic string.
            payload (dict): Parsed JSON payload containing device health info.
        Output:
            None. Updates device status in database.
        """
        device_id = payload.get('device_id')
        timestamp = payload.get('timestamp')
        battery_level = payload.get('metadata', {}).get('battery_level_pct')
        
        # Why: Heartbeat messages indicate device is alive and connected
        # Used to detect offline devices and monitor battery status
        # Important for remote deployments with battery-powered sensors
        
        logger.info("Heartbeat from device %s at %s (battery: %s%%)", 
                   device_id, timestamp, battery_level)
        
        # TO-DO: Update device status in Relational DB
        # Track:
        # - last_seen: timestamp of last heartbeat
        # - status: "online" | "offline" | "warning"
        # - battery_level: for power management
        #
        # Example:
        # status = "online" if battery_level > 20 else "warning"
        # self.device_db.update(
        #     device_id=device_id,
        #     last_seen=timestamp,
        #     status=status,
        #     battery_level=battery_level
        # )
        
        # TO-DO: Implement offline detection
        # If no heartbeat received for X minutes, mark device as "offline"
        # and trigger alert to maintenance team
