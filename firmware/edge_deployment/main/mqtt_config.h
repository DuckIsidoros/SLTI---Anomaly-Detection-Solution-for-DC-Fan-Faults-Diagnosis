#pragma once

#include <stdint.h>

// Wifi configuration.
#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI_SSID"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

// MQTT Broker set up.
#ifndef MQTT_BROKER_HOST
#define MQTT_BROKER_HOST "192.168.1.100"
#endif

#ifndef MQTT_BROKER_PORT
// Port 8883 for MQTTS (TLS 1.2/1.3), Port 1883 for unencrypted development
#define MQTT_BROKER_PORT 8883
#endif

#ifndef MQTT_USE_TLS
#define MQTT_USE_TLS 1
#endif

// Credentials configured in dashboard/mosquitto/config/passwd and acl
#ifndef MQTT_USERNAME
#define MQTT_USERNAME "edge_node_01"
#endif

#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD "EdgePass2026!"
#endif

#ifndef MQTT_CLIENT_ID
#define MQTT_CLIENT_ID "esp32_edge_01"
#endif

#ifndef DEVICE_ID
#define DEVICE_ID "esp32_edge_01"
#endif

// Plan the sample format following the document.
#define PLANT_ID "plant_hcm"
#define LINE_ID "line_01"
#define MACHINE_ID "cnc_05"
#define SENSOR_TYPE "accelerometer"

#define MQTT_TOPIC_PREFIX "v1/" PLANT_ID "/" LINE_ID "/" MACHINE_ID

// Concrete topic paths
#define MQTT_TOPIC_TELEMETRY MQTT_TOPIC_PREFIX "/" SENSOR_TYPE "/telemetry"
#define MQTT_TOPIC_ALERT MQTT_TOPIC_PREFIX "/" SENSOR_TYPE "/alert"
#define MQTT_TOPIC_HEARTBEAT MQTT_TOPIC_PREFIX "/" SENSOR_TYPE "/heartbeat"
#define MQTT_TOPIC_STATUS MQTT_TOPIC_PREFIX "/status"

// Quality of Service (QoS) specifications from dashboard/mqtt_hierarchy.md
#define MQTT_QOS_TELEMETRY 0
#define MQTT_QOS_ALERT 1
#define MQTT_QOS_HEARTBEAT 0
#define MQTT_QOS_STATUS 1

// Heartbeat transmission period in milliseconds
#define HEARTBEAT_INTERVAL_MS 30000

// Root CA certificate for MQTTS
static const char ROOT_CA_CERT_PEM[] PROGMEM =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIFnTCCA4WgAwIBAgIUJ2die4t9wN4/ETDs4zVvpvUd6I4wDQYJKoZIhvcNAQEL\n"
    "BQAwXjELMAkGA1UEBhMCVk4xDDAKBgNVBAgMA0hDTTESMBAGA1UEBwwJSG9DaGlN\n"
    "aW5oMRcwFQYDVQQKDA5JbmR1c3RyaWFsX0lvVDEUMBIGA1UEAwwLSW9UX1Jvb3Rf\n"
    "Q0EwHhcNMjYxMDIwMTM0ODQyWhcNMzYxMDE3MTM0ODQyWjBeMQswCQYDVQQGEwJW\n"
    "TjEMMAoGA1UECAwDSENNMRIwEAYDVQQHDAlIb0NoaU1pbmgxFzAVBgNVBAoMDklu\n"
    "ZHVzdHJpYWxfSW9UMRQwEgYDVQQDDAtJb1RfUm9vdF9DQTCCAiIwDQYJKoZIhvcN\n"
    "AQEBBQADggIPADCCAgoCggIBAO2DPksoZYFgl73vx9nd6zCG/fh9XhqVh+fcXiaV\n"
    "rGncPNLp0Gyv2OOYnjwabkO4iCDHyMidbMlqLSq99nLQ5yR1DBO67TVwb73yYFN/\n"
    "LVFKzqn14M5WaZqGxnJVfNUVMOgJzwPKpSkmYDomlCq2cE+ztXJm+e9HOqLVAW6i\n"
    "9ejM/eU2cBxtYNfrsFC+T7BYXcNQxu8/j7h7ANpX366VH3ZdouA5hag55WoFL3IB\n"
    "l/LB5y6iPUFyI0F+ywdzCY/TmKh5aAxyMaTRDS+2TghOzxnxPD7gbxgr/q9Dyaqr\n"
    "pywYNQFPflfdf0SL7xVxhfM4dKd4Cz0ESwx37Lm6uWK0QcpsLV9emZPq0paReUjd\n"
    "JvIsR4tnzpDgl9lyA8/7iIj0UavbSvpzMh0jqpdHcEd8DBIWVL3zlI+91g8MdKsV\n"
    "R6QG4ojhFUULmXdWhQOrIaFhvtkP7HfpEkB9AoGhsNSX2m2GgoKDv5Pp+M0O+OUn\n"
    "l652fNX4F65Ys/fx+I1jGyUQWYpU6crotpOSqPdZt2rkSxGQ5o7qC35RvnElmlUb\n"
    "p7bDWliKQMfoMewC7VdTw3kDDOkncFnl1ba2qxM9dihS/rpBif5mxMUtYHNcfYjx\n"
    "+wLUsBevIgv3+gtFepblfB/gsgx7kXi30GMvMU4B/VqrInSuIoBeGe7aUJEVdLOl\n"
    "65ThAgMBAAGjUzBRMB0GA1UdDgQWBBSFgADUZcQeBV7n3R/s8zrLTy/DkzAfBgNV\n"
    "HSMEGDAWgBSFgADUZcQeBV7n3R/s8zrLTy/DkzAPBgNVHRMBAf8EBTADAQH/MA0G\n"
    "CSqGSIb3DQEBCwUAA4ICAQAhB1m0/nrNTz1pATgzjAjfwCGrD/y2TLpJVD4PM+O3\n"
    "z47R3p2jXPiyjwENauvR+GZTUU2WHW7a7WIPASuozrzzLIYY2xY+RZAbwIZmkrwv\n"
    "U4rSM5i7xTwwTeImtucFkpm7urYNohOWTbZEOOjUlqUsX6X1tIFtrNCIDV9yMH2B\n"
    "s7YeOAOxwg9DOdefh2HCeKkFEUMmQd4qOc6AeTlPfi2d/HUTk+W2xKBphVWNxvxj\n"
    "/7LcE0Wdau1wQT6YrkQGxBd/a9gzDlR/1+qFmmLp0HIFr+C9MhAtRrQdi9Kxl9Z8\n"
    "EuN4mHgCj7BgOmL7hsQ00XERZ5NsYdvwYawa/TxtOiYccIqlw3k1kKyXTrq6njMa\n"
    "K/3TNlAy1JX5lbTXO0vVIgfh8MmYzS3NxydUMPRuw1YV9phb4fdTjl6H9IuajRjH\n"
    "E9qwBFPOr1eoi9nGqiRJBquB6zzZDGABHUYSI+F4e9Fq0HxbUT/IuUx5vCYkrEC+\n"
    "L0iZB82Muu7U97+4EzB8hwYYX9wAtSW/izzyfy3F+LxVL09/hJf/s0Vab9/BFIx5\n"
    "6O2/oYu/bKj1YvbFV3fbE0cAq2UbT+Yv6E30tRbzfaHZ3PYXP5wzX+EO/s9K/ozQ\n"
    "Zqt9DmHbJMHg8MnyJioQ0VahQOeuuhWxb0W4taYD3j1CZZMdQNCVTwdhq2Iqsdhd\n"
    "gw==\n"
    "-----END CERTIFICATE-----\n";
