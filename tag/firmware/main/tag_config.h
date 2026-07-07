#pragma once

/* BLE advertising parameters */
#define BLE_ADV_INTERVAL_MS       100
#define BLE_TX_POWER_DBM          9
#define BLE_TX_POWER_1M_DBM       (-71)

/* Tag identity */
#define TAG_UUID  "019d8ba3-5d5f-792b-8e60-906fbeca324a"
#define TAG_DEVICE_NAME "IPT-TAG-01"

/* GPIO — WS2812B RGB LED on ESP32-S3-DevKitC-1 */
#define LED_STATUS_GPIO           48

/* Watchdog */
#define WDT_TIMEOUT_SEC           5

/* NVS persistence */
#define NVS_UPTIME_PERSIST_SEC    60
