"""
MQTT Client Module for Backend Consumer Service.
Handles connection, subscription, and message reception from Azure IoT Hub.
"""

import paho.mqtt.client as mqtt
import yaml
import logging

logger = logging.getLogger(__name__)


class MQTTClient:
    """
    MQTT Client wrapper for Azure IoT Hub connection.
    """
    
    def __init__(self, config_path: str, on_message_callback: callable):
        """
        Initialize MQTT client with configuration.
        
        Input:
            config_path (str): Path to mqtt_config.yaml file.
            on_message_callback (callable): Function to handle received messages.
        Output:
            None. Initializes internal client state.
        """
        self.config = self._load_config(config_path)
        self.on_message_callback = on_message_callback
        self.client = self._create_client()
        
    def _load_config(self, config_path: str) -> dict:
        """
        Load MQTT configuration from YAML file.
        
        Input:
            config_path (str): Path to config file.
        Output:
            dict: Parsed configuration dictionary.
        """
        with open(config_path, 'r') as f:
            return yaml.safe_load(f)
    
    def _create_client(self) -> mqtt.Client:
        """
        Create and configure MQTT client instance.
        
        Input:
            None.
        Output:
            mqtt.Client: Configured MQTT client.
        """
        client_config = self.config['client']
        
        # Create client with specified protocol
        client = mqtt.Client(
            client_id=client_config['client_id'],
            protocol=getattr(mqtt, client_config['protocol'])
        )
        
        # Bind callbacks to handle connection lifecycle and incoming data
        client.on_connect = self._on_connect
        client.on_message = self._on_message
        client.on_disconnect = self._on_disconnect
        
        # Configure authentication based on config
        auth_config = self.config['authentication']
        if auth_config['method'] == 'connection_string':
            self._configure_connection_string_auth(client, auth_config['connection_string'])
        elif auth_config['method'] == 'x509_certificate':
            # TO-DO: Implement X.509 certificate authentication for IEC 62443 compliance
            logger.warning("X.509 authentication not yet implemented")
        
        return client
    
    def _configure_connection_string_auth(self, client: mqtt.Client, conn_string: str):
        """
        Configure authentication using Azure IoT Hub connection string.
        
        Input:
            client (mqtt.Client): MQTT client instance.
            conn_string (str): Azure IoT Hub connection string.
        Output:
            None.
        """
        # Why: Azure IoT Hub requires specific username/password format derived from the connection string.
        # Standard MQTT brokers just use raw username/password, but IoT Hub needs this parsing.
        parts = dict(item.split('=') for item in conn_string.split(';'))
        
        hostname = parts['HostName']
        device_id = parts['DeviceId']
        sas_key = parts['SharedAccessKey']
        
        # Azure IoT Hub requires username format: {hostname}/{device_id}
        username = f"{hostname}/{device_id}"
        client.username_pw_set(username=username, password=sas_key)
        
    def _on_connect(self, client, userdata, flags, rc):
        """
        Callback when connection to broker is established.
        
        Input:
            client: MQTT client instance.
            userdata: User data (not used).
            flags: Response flags from broker.
            rc (int): Return code (0 = success).
        Output:
            None.
        """
        if rc == 0:
            logger.info("Successfully connected to MQTT broker")
            # Subscribe to all configured topics
            for sub_config in self.config['subscription']['topics']:
                topic = sub_config['topic']
                qos = sub_config['qos']
                
                # Why: QoS 1 ensures at-least-once delivery. 
                # Necessary for the whole system so we don't miss critical machine alerts.
                client.subscribe(topic, qos=qos)
                logger.info("Subscribed to topic: %s (QoS %d)", topic, qos)
        else:
            logger.error("Connection failed with return code %d", rc)
    
    def _on_message(self, client, userdata, msg):
        """
        Callback when message is received from broker.
        
        Input:
            client: MQTT client instance.
            userdata: User data (not used).
            msg: MQTTMessage object containing topic and payload.
        Output:
            None. Routes message to handler callback.
        """
        # Why: Delegate processing to external handler to keep this class focused only on network logic.
        self.on_message_callback(client, userdata, msg)
    
    def _on_disconnect(self, client, userdata, rc):
        """
        Callback when disconnected from broker.
        
        Input:
            client: MQTT client instance.
            userdata: User data (not used).
            rc (int): Return code (0 = graceful disconnect).
        Output:
            None.
        """
        if rc != 0:
            logger.warning("Unexpected disconnection (rc=%d). Will attempt reconnect.", rc)
        else:
            logger.info("Disconnected from MQTT broker")
    
    def start(self):
        """
        Start MQTT client connection loop.
        
        Input:
            None.
        Output:
            None. Runs indefinitely until stopped.
        """
        broker_config = self.config['broker']
        
        # TO-DO: Add TLS configuration for production (IEC 62443 requires encrypted transport)
        # self.client.tls_set(ca_certs="certs/ca.pem", certfile="certs/client.pem", keyfile="certs/client.key")
        
        self.client.connect(
            host=broker_config['url'],
            port=broker_config['port'],
            keepalive=broker_config['keepalive']
        )
        
        logger.info("Starting MQTT client loop...")
        # loop_forever() handles automatic reconnection if the network drops
        self.client.loop_forever()
    
    def stop(self):
        """
        Stop MQTT client and disconnect gracefully.
        
        Input:
            None.
        Output:
            None.
        """
        self.client.disconnect()
        self.client.loop_stop()
        logger.info("MQTT client stopped")
