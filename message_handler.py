# Thêm import
from database_writer import DatabaseWriter

# Trong class MessageHandler, thêm __init__:
class MessageHandler:
    def __init__(self, db_writer: DatabaseWriter):
        """
        Input:
            db_writer (DatabaseWriter): Instance for writing to DBs
        """
        self.db_writer = db_writer
    
    def _handle_telemetry(self, topic: str, payload: dict):
        # ... existing code ...
        
        # Write to Time-series DB
        if self.db_writer:
            self.db_writer.write_telemetry(payload)
    
    def _handle_alert(self, topic: str, payload: dict):
        # ... existing code ...
        
        # Write to Relational DB
        if self.db_writer:
            self.db_writer.write_alert(payload)
