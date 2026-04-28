#include "nvs_log.h"
#include "tag_config.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_log.h"

#define NVS_NAMESPACE   "tag_nvs"
#define KEY_RESET_COUNT "reset_count"
#define KEY_UPTIME_SEC  "uptime_sec"

static const char *TAG = "nvs_log";
static uint32_t s_boot_uptime_sec = 0;

void nvs_log_init(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return;
    }

    uint32_t count = 0;
    nvs_get_u32(h, KEY_RESET_COUNT, &count); /* ignore NOT_FOUND — starts at 0 */
    count++;
    ESP_ERROR_CHECK(nvs_set_u32(h, KEY_RESET_COUNT, count));

    uint32_t uptime = 0;
    nvs_get_u32(h, KEY_UPTIME_SEC, &uptime);
    s_boot_uptime_sec = uptime;

    ESP_ERROR_CHECK(nvs_commit(h));
    nvs_close(h);

    ESP_LOGI(TAG, "boot #%lu, cumulative uptime %lu s", (unsigned long)count, (unsigned long)uptime);
}

static void uptime_timer_cb(void *arg)
{
    (void)arg;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;

    uint64_t now_us = esp_timer_get_time();
    uint32_t session_sec = (uint32_t)(now_us / 1000000ULL);
    uint32_t total = s_boot_uptime_sec + session_sec;

    nvs_set_u32(h, KEY_UPTIME_SEC, total);
    nvs_commit(h);
    nvs_close(h);
}

void nvs_log_start_uptime_timer(void)
{
    esp_timer_handle_t timer;
    const esp_timer_create_args_t args = {
        .callback = uptime_timer_cb,
        .name     = "nvs_uptime",
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, NVS_UPTIME_PERSIST_SEC * 1000000ULL));
    ESP_LOGI(TAG, "Uptime persist timer started (every %d s)", NVS_UPTIME_PERSIST_SEC);
}
