#include "ota_update.h"
#include "ble_adv.h"
#include "tag_config.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "ota_update";
static EventGroupHandle_t s_wifi_events;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static bool wifi_connect(void)
{
    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t h_any, h_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        wifi_event_handler, NULL, &h_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        wifi_event_handler, NULL, &h_got_ip));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid     = OTA_WIFI_SSID,
            .password = OTA_WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(OTA_CONN_TIMEOUT_SEC * 1000));

    bool connected = (bits & WIFI_CONNECTED_BIT) != 0;
    if (!connected) {
        ESP_LOGE(TAG, "WiFi connect failed or timed out");
    }
    return connected;
}

static void wifi_cleanup(void)
{
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
}

static bool run_ota_download(void)
{
    esp_ota_handle_t ota_handle;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition found");
        return false;
    }

    ESP_LOGI(TAG, "Writing to partition: %s", update_partition->label);

    esp_http_client_config_t http_cfg = {
        .url            = OTA_SERVER_URL,
        .timeout_ms     = OTA_DOWNLOAD_TIMEOUT_SEC * 1000,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int content_len = esp_http_client_fetch_headers(client);
    ESP_LOGI(TAG, "OTA image size: %d bytes", content_len);

    ESP_ERROR_CHECK(esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle));

    bool success = false;
    char buf[1024];
    int total = 0;
    while (true) {
        int read = esp_http_client_read(client, buf, sizeof(buf));
        if (read < 0) {
            ESP_LOGE(TAG, "HTTP read error");
            break;
        }
        if (read == 0) {
            success = true;
            break;
        }
        if (esp_ota_write(ota_handle, buf, read) != ESP_OK) {
            ESP_LOGE(TAG, "OTA write failed");
            break;
        }
        total += read;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (!success || esp_ota_end(ota_handle) != ESP_OK) {
        ESP_LOGE(TAG, "OTA end failed or download incomplete (%d bytes)", total);
        return false;
    }

    ESP_ERROR_CHECK(esp_ota_set_boot_partition(update_partition));
    ESP_LOGI(TAG, "OTA complete (%d bytes). Rebooting.", total);
    return true;
}

void ota_check_and_run(void)
{
    /* Configure pull-up before sampling to prevent spurious trigger on custom PCB */
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << OTA_TRIGGER_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    if (gpio_get_level(OTA_TRIGGER_GPIO) != 0) {
        return; /* Normal boot */
    }

    ESP_LOGI(TAG, "OTA trigger detected — entering OTA mode");

    ble_adv_stop();

    if (!wifi_connect()) {
        wifi_cleanup();
        esp_restart();
    }

    bool ok = run_ota_download();
    wifi_cleanup();
    esp_restart(); /* Reboot on both success and failure; bootloader handles rollback */
    (void)ok;
}
