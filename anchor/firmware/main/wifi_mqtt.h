#pragma once
#include <stdbool.h>

/* Init WiFi, SNTP, and MQTT client. Blocks until fully connected or reboots. */
void wifi_mqtt_init(void);

/* Reconnect if WiFi or MQTT is down. Returns false when all 5 retries exhausted. */
bool wifi_mqtt_ensure_connected(void);

/* Publish EWA-filtered RSSI — QoS 0, no retain. */
void wifi_mqtt_publish_rssi(float rssi);

/* Publish null-RSSI (tag absent) — QoS 1, no retain. */
void wifi_mqtt_publish_null_rssi(void);

/* Publish heartbeat — QoS 1, retain. */
void wifi_mqtt_publish_heartbeat(void);
