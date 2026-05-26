#include "anchor_config.h"
#include "ble_scan.h"
#include "led_status.h"
#include "ota_update.h"
#include "wifi_mqtt.h"

#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_main";

void app_main(void)
{
    /* 1. NVS flash init — required before BLE and MQTT */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* 2. Commit firmware on first post-OTA boot; no-op on cold boot */
    esp_ota_mark_app_valid_cancel_rollback();

    /* 3. Reconfigure TWDT and register main task */
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms    = ANCHOR_WDT_TIMEOUT_SEC * 1000,
        .trigger_panic = true,
    };
    ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&wdt_cfg));
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    /* 4. LED — slow amber blink while connecting */
    led_status_init();
    led_status_start();
    led_status_set(LED_CONNECTING);

    /* 5. OTA check — returns immediately if trigger not asserted */
    ota_check_and_run();

    /* 6. WiFi + SNTP + MQTT — blocks until connected (LED → solid green on CONNACK) */
    wifi_mqtt_init();

    /* 7. BLE passive scan — scanning starts from on_sync callback */
    ble_scan_init();

    ESP_LOGI(TAG, "Boot complete — anchor %s running", ANCHOR_ID);

    /* 8. Main scan-publish loop at ANCHOR_SCAN_INTERVAL_MS cadence */
    int64_t last_heartbeat_ms = 0;

    while (true) {
        esp_task_wdt_reset();

        if (!wifi_mqtt_ensure_connected()) {
            /* All 5 retries exhausted — controlled reboot */
            led_status_set(LED_REBOOT);
            vTaskDelay(pdMS_TO_TICKS(700)); /* allow 3-blink sequence to complete */
            esp_restart();
        }

        ble_scan_result_t result;
        ble_scan_get_result(&result);

        if (result.tag_detected) {
            wifi_mqtt_publish_rssi(result.rssi_filtered);
        } else {
            wifi_mqtt_publish_null_rssi();
            ble_scan_reset_ewa();
        }

        /* Heartbeat every ANCHOR_HEARTBEAT_INTERVAL_S seconds */
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - last_heartbeat_ms >= (int64_t)ANCHOR_HEARTBEAT_INTERVAL_S * 1000) {
            wifi_mqtt_publish_heartbeat();
            last_heartbeat_ms = now_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(ANCHOR_SCAN_INTERVAL_MS));
    }
}
