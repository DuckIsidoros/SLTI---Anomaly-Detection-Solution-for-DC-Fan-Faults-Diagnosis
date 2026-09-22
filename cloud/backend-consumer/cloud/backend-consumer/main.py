"""
Main Entry Point for Backend Consumer Service.
Initializes MQTT client and starts message processing loop.
"""

import logging
import yaml
from mqtt_client import MQTTClient
from message_handler import MessageHandler

# Load logging configuration
with open('config/mqtt_config.yaml', 'r') as f:
    config = yaml.safe_load(f)

logging.basicConfig(
    level=getattr(logging, config['logging']['level']),
    format=config['logging']['format']
)
logger = logging.getLogger(__name__)


def main():
    """
    Main function to start the backend consumer service.
    
    Input:
        None
    Output:
        None. Runs indefinitely.
    """
    logger.info("Starting Backend Consumer Service...")
    
    # Initialize message handler
    handler = MessageHandler()
    
    # Initialize MQTT client with handler callback
    mqtt_client = MQTTClient(
        config_path='config/mqtt_config.yaml',
        on_message_callback=lambda client, userdata, msg: 
            handler.handle_message(msg.topic, msg.payload)
    )
    
    try:
        # Start MQTT client (blocks indefinitely)
        mqtt_client.start()
    except KeyboardInterrupt:
        logger.info("Shutting down gracefully...")
        mqtt_client.stop()
    except Exception as e:
        logger.error("Fatal error: %s", str(e))
        mqtt_client.stop()
        raise


if __name__ == "__main__":
    main()
