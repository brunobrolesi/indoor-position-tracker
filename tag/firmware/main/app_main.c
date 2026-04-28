#include "tag_config.h"
#include "ble_adv.h"
#include "led_status.h"
#include "nvs_log.h"
#include "ota_update.h"

#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_main";

void app_main(void)
{
    /* 1. NVS flash init — required before BLE and NVS modules */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* 2. Confirm this firmware is healthy — no-op on cold boot, commits new
     *    firmware on first boot after OTA (prevents silent rollback on next reboot). */
    esp_ota_mark_app_valid_cancel_rollback();

    /* 3. Watchdog — already init by IDF startup; just reconfigure timeout and register main task */
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms     = WDT_TIMEOUT_SEC * 1000,
        .trigger_panic  = true,
    };
    ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&wdt_cfg));
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL)); /* Register main task */

    /* 4. NVS: read/increment reset counter */
    nvs_log_init();

    /* 5-6. LED heartbeat */
    led_status_init();
    led_status_start();

    /* 7. OTA check — returns immediately if trigger not asserted */
    ota_check_and_run();

    /* 8. BLE advertising — ble_adv_start() is called from the on_sync callback */
    ble_adv_init();

    /* 10. NVS uptime persist timer */
    nvs_log_start_uptime_timer();

    ESP_LOGI(TAG, "Boot complete — running");

    /* 11. Main loop: feed watchdog every ~1 second */
    while (true) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
