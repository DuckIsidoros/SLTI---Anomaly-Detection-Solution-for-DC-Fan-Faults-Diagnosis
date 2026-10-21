#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo "=== Preparing Mosquitto Docker Environment ==="

# 1. Ensure required directories exist
mkdir -p data log certs config

# 2. Check if certificates exist; if not, generate them
if [ ! -f "certs/server.crt" ] || [ ! -f "certs/server.key" ]; then
    echo "Certificates not found. Running generate_certs.sh..."
    bash ./generate_certs.sh
fi

# 3. Ensure proper directory permissions for Mosquitto user (UID 1883) inside container
chmod 777 data log
chmod 644 config/mosquitto.conf config/acl
chmod 600 config/passwd 2>/dev/null || chmod 644 config/passwd

echo "=== Starting Mosquitto Container ==="
if command -v docker &> /dev/null; then
    docker compose up -d
    echo "Mosquitto is running!"
    echo "  - MQTTS (TCP+TLS): Port 8883"
    echo "  - WSS (WebSockets): Port 8084"
    echo "Check logs with: docker compose logs -f"
else
    echo "Docker command not found on this system. Please run 'docker compose up -d' on your Docker host."
fi
