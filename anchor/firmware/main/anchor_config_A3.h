#pragma once
#include "driver/gpio.h"

#define ANCHOR_ID                          "A3"
#define ANCHOR_POS_X                       0.0f
#define ANCHOR_POS_Y                       0.0f
#define ANCHOR_WIFI_SSID                   "CHANGE_ME"
#define ANCHOR_WIFI_PASSWORD               "CHANGE_ME"
#define ANCHOR_MQTT_HOST                   "192.168.1.100"
#define ANCHOR_MQTT_PORT                   1883
#define ANCHOR_MQTT_USER                   "CHANGE_ME"
#define ANCHOR_MQTT_PASSWORD               "CHANGE_ME"
#define ANCHOR_MQTT_TLS_ENABLED            false
#define ANCHOR_MQTT_CLIENT_ID              "anchor_A3"
#define ANCHOR_NTP_SERVER                  "pool.ntp.org"
#define ANCHOR_OTA_SERVER_URL              "http://192.168.1.100:8000/anchor.bin"
#define ANCHOR_OTA_TRIGGER_GPIO            GPIO_NUM_0
#define ANCHOR_SCAN_INTERVAL_MS            200
#define ANCHOR_EWA_ALPHA                   0.3f
_Static_assert(ANCHOR_EWA_ALPHA >= 0.1f && ANCHOR_EWA_ALPHA <= 1.0f, \
               "ANCHOR_EWA_ALPHA must be in [0.1, 1.0]");
#define ANCHOR_WIFI_CONNECT_TIMEOUT_MS     10000
#define ANCHOR_OTA_WIFI_CONNECT_TIMEOUT_MS 30000
#define ANCHOR_SNTP_SYNC_TIMEOUT_MS        5000
#define ANCHOR_MQTT_CONNECT_TIMEOUT_MS     5000
#define ANCHOR_TAG_TIMEOUT_MS              2000
#define ANCHOR_HEARTBEAT_INTERVAL_S        10
#define ANCHOR_WDT_TIMEOUT_SEC             10
/* GPIO 48 = onboard WS2812B on ESP32-S3-DevKitC-1; requires RMT peripheral */
#define ANCHOR_LED_GPIO                    GPIO_NUM_48
