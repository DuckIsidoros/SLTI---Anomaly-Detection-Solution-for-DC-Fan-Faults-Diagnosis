#!/usr/bin/env bash
set -e

CERTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/certs"
mkdir -p "$CERTS_DIR"
cd "$CERTS_DIR"

echo "=== 1. Generating Root CA ==="
openssl genrsa -out ca.key 4096
openssl req -x509 -new -nodes -key ca.key -sha256 -days 3650 \
  -subj "/C=VN/ST=HCM/L=HoChiMinh/O=Industrial_IoT/CN=IoT_Root_CA" \
  -out ca.crt

echo "=== 2. Generating Server Certificate with SAN ==="
openssl genrsa -out server.key 2048

# Create OpenSSL config with Subject Alternative Names (SAN)
cat > server_ext.cnf <<EOF
[req]
default_bits = 2048
prompt = no
default_md = sha256
req_extensions = req_ext
distinguished_name = dn

[dn]
C = VN
ST = HCM
L = HoChiMinh
O = Industrial_IoT
CN = localhost

[req_ext]
subjectAltName = @alt_names

[alt_names]
DNS.1 = localhost
DNS.2 = mqtt.local
DNS.3 = mosquitto-broker
IP.1 = 127.0.0.1
IP.2 = 0.0.0.0
EOF

openssl req -new -key server.key -out server.csr -config server_ext.cnf
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -out server.crt -days 1825 -sha256 -extfile server_ext.cnf -extensions req_ext

echo "=== 3. Generating Client Test Certificate ==="
openssl genrsa -out client.key 2048
openssl req -new -key client.key \
  -subj "/C=VN/ST=HCM/L=HoChiMinh/O=Industrial_IoT/CN=edge_node_01" \
  -out client.csr
openssl x509 -req -in client.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -out client.crt -days 1825 -sha256

# Cleanup CSR and temporary configs
rm -f server.csr client.csr server_ext.cnf

# Set secure permissions
chmod 644 ca.crt server.crt client.crt
chmod 600 ca.key server.key client.key

echo "=== Certificates successfully generated in $CERTS_DIR ==="
