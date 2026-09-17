"""
Database Writer Module - Handles data persistence to Time-series & Relational DBs.
"""

from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS
from datetime import datetime
import logging

logger = logging.getLogger(__name__)


class DatabaseWriter:
    """
    Writes data to InfluxDB (Time-series) and PostgreSQL (Relational).
    """
    
    def __init__(self, influx_config: dict):
        """
        Input:
            influx_config (dict): InfluxDB connection config
                - url: InfluxDB Cloud URL
                - token: API token
                - org: Organization name
                - bucket: Bucket name
        Output:
            None
        """
        self.influx_config = influx_config
        self.influx_client = None
        self.write_api = None
        
    def connect_influxdb(self):
        """
        Establish connection to InfluxDB Cloud.
        Input: None
        Output: None
        """
        try:
            self.influx_client = InfluxDBClient(
                url=self.influx_config['url'],
                token=self.influx_config['token'],
                org=self.influx_config['org']
            )
            self.write_api = self.influx_client.write_api(write_options=SYNCHRONOUS)
            logger.info("Connected to InfluxDB successfully")
        except Exception as e:
            logger.error("Failed to connect to InfluxDB: %s", str(e))
            raise
    
    def write_telemetry(self, payload: dict):
        """
        Write telemetry data to InfluxDB (Time-series).
        
        Input:
            payload (dict): Parsed telemetry payload
                {
                    "device_id": "edge_node_01",
                    "timestamp": "2026-08-22T10:00:00.123Z",
                    "payload": {
                        "vibration": {"x_rms": 2.45, "y_rms": 1.89, "z_rms": 3.10},
                        "environment": {"temperature": 45.2, "humidity": 60.5}
                    }
                }
        Output:
            None
        """
        try:
            # Create InfluxDB Point
            point = Point("machine_vibration") \
                .tag("device_id", payload['device_id']) \
                .tag("machine_id", payload.get('machine_id', 'unknown')) \
                .field("x_rms", payload['payload']['vibration']['x_rms']) \
                .field("y_rms", payload['payload']['vibration']['y_rms']) \
                .field("z_rms", payload['payload']['vibration']['z_rms']) \
                .field("temperature", payload['payload'].get('environment', {}).get('temperature', 0)) \
                .field("humidity", payload['payload'].get('environment', {}).get('humidity', 0)) \
                .time(payload['timestamp'])
            
            # Write to bucket
            self.write_api.write(
                bucket=self.influx_config['bucket'],
                record=point
            )
            logger.debug("Telemetry written for device %s", payload['device_id'])
            
        except Exception as e:
            logger.error("Failed to write telemetry: %s", str(e))
    
    def write_alert(self, payload: dict):
        """
        Write alert data to PostgreSQL (Relational DB).
        # TO-DO: Implement Supabase/PostgreSQL connection
        Input:
            payload (dict): Alert payload with edge inference
        Output:
            None
        """
        logger.info("Alert received - TO-DO: Write to PostgreSQL")
        # TO-DO: Implement Supabase client
        # Example:
        # supabase.table('alerts').insert({
        #     'device_id': payload['device_id'],
        #     'timestamp': payload['timestamp'],
        #     'fault_type': payload['payload']['edge_inference']['fault_type'],
        #     'anomaly_score': payload['payload']['edge_inference']['anomaly_score']
        # }).execute()
    
    def close(self):
        """
        Close database connections.
        Input: None
        Output: None
        """
        if self.write_api:
            self.write_api.close()
        if self.influx_client:
            self.influx_client.close()
        logger.info("Database connections closed")
