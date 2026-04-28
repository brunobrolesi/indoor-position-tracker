#pragma once

/* BLE advertising parameters */
#define BLE_ADV_INTERVAL_MS       100
#define BLE_TX_POWER_DBM          0
/* TODO: measure at 1m in bench test before first field flash */
#define BLE_TX_POWER_1M_DBM       (-65)

/* Tag identity */
#define TAG_UUID  "019d8ba3-5d5f-792b-8e60-906fbeca324a"

/* GPIO — WS2812B RGB LED on ESP32-S3-DevKitC-1 */
#define LED_STATUS_GPIO           48
#define OTA_TRIGGER_GPIO          0

/* WiFi credentials for OTA — MVP only, replace with provisioning in future */
#define OTA_WIFI_SSID             "CHANGE_ME"
#define OTA_WIFI_PASSWORD         "CHANGE_ME"

/* OTA server */
#define OTA_SERVER_URL            "http://192.168.1.100:8070/tag-firmware.bin"
#define OTA_CONN_TIMEOUT_SEC      30
#define OTA_DOWNLOAD_TIMEOUT_SEC  60

/* Watchdog */
#define WDT_TIMEOUT_SEC           5

/* NVS persistence */
#define NVS_UPTIME_PERSIST_SEC    60
