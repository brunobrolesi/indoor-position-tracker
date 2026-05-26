#include "wifi_mqtt.h"
#include "led_status.h"
#include "anchor_config.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

static const char *TAG = "wifi_mqtt";

#define WIFI_CONNECTED_BIT BIT0
#define MQTT_CONNECTED_BIT BIT1

static EventGroupHandle_t    s_net_events;
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static int                   s_fail_count = 0;

/* --------------------------------------------------------------------------
 * WiFi event handler
 * -------------------------------------------------------------------------- */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_net_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "WiFi disconnected");
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_net_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "WiFi got IP");
    }
}

/* --------------------------------------------------------------------------
 * MQTT event handler
 * -------------------------------------------------------------------------- */

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)data;
    (void)arg;
    (void)base;

    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        xEventGroupSetBits(s_net_events, MQTT_CONNECTED_BIT);
        led_status_set(LED_CONNECTED);
        ESP_LOGI(TAG, "MQTT connected");
        break;
    case MQTT_EVENT_DISCONNECTED:
        xEventGroupClearBits(s_net_events, MQTT_CONNECTED_BIT);
        ESP_LOGW(TAG, "MQTT disconnected");
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error: type=%d",
                 event->error_handle->error_type);
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------
 * WiFi init (one-time, called from wifi_mqtt_init)
 * -------------------------------------------------------------------------- */

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_cfg = {};
    strncpy((char *)wifi_cfg.sta.ssid,     ANCHOR_WIFI_SSID,     sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, ANCHOR_WIFI_PASSWORD, sizeof(wifi_cfg.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* --------------------------------------------------------------------------
 * Connect steps (used by both init and reconnect)
 * -------------------------------------------------------------------------- */

/* Poll a condition with 500 ms ticks so the main task can reset the WDT
 * during blocking waits longer than ANCHOR_WDT_TIMEOUT_SEC. */
static bool wait_for_bit(EventGroupHandle_t eg, EventBits_t bit, uint32_t timeout_ms)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (esp_timer_get_time() < deadline) {
        esp_task_wdt_reset();
        EventBits_t bits = xEventGroupWaitBits(eg, bit, pdFALSE, pdTRUE,
                                               pdMS_TO_TICKS(500));
        if (bits & bit) return true;
    }
    return false;
}

static bool step_wifi_connect(void)
{
    if (xEventGroupGetBits(s_net_events) & WIFI_CONNECTED_BIT) return true;

    esp_wifi_connect();

    if (!wait_for_bit(s_net_events, WIFI_CONNECTED_BIT, ANCHOR_WIFI_CONNECT_TIMEOUT_MS)) {
        ESP_LOGE(TAG, "WiFi connect timeout");
        return false;
    }
    return true;
}

static bool step_sntp_sync(void)
{
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(ANCHOR_NTP_SERVER);
    esp_netif_sntp_init(&sntp_cfg);

    /* Poll with WDT resets every 200 ms */
    int64_t deadline = esp_timer_get_time() + (int64_t)ANCHOR_SNTP_SYNC_TIMEOUT_MS * 1000;
    while (esp_timer_get_time() < deadline) {
        esp_task_wdt_reset();
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            ESP_LOGI(TAG, "SNTP synced");
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    ESP_LOGE(TAG, "SNTP sync timeout");
    esp_netif_sntp_deinit();
    return false;
}

static bool step_mqtt_connect(void)
{
    if (xEventGroupGetBits(s_net_events) & MQTT_CONNECTED_BIT) return true;

    esp_mqtt_client_reconnect(s_mqtt_client);

    if (!wait_for_bit(s_net_events, MQTT_CONNECTED_BIT, ANCHOR_MQTT_CONNECT_TIMEOUT_MS)) {
        ESP_LOGE(TAG, "MQTT connect timeout");
        return false;
    }
    return true;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void wifi_mqtt_init(void)
{
    s_net_events = xEventGroupCreate();
    configASSERT(s_net_events);

    wifi_init_sta();

    /* Wait for WiFi + SNTP */
    if (!step_wifi_connect()) {
        ESP_LOGE(TAG, "Initial WiFi connect failed — retrying via reconnect loop");
    } else {
        step_sntp_sync(); /* best-effort; publish timestamps may be off until sync */
    }

    /* Init MQTT client */
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname          = ANCHOR_MQTT_HOST,
        .broker.address.port              = ANCHOR_MQTT_PORT,
        .broker.address.transport         = MQTT_TRANSPORT_OVER_TCP,
        .credentials.username             = ANCHOR_MQTT_USER,
        .credentials.authentication.password = ANCHOR_MQTT_PASSWORD,
        .credentials.client_id            = ANCHOR_MQTT_CLIENT_ID,
    };
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    configASSERT(s_mqtt_client);

    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);

    /* Wait for MQTT CONNACK */
    EventBits_t bits = xEventGroupWaitBits(s_net_events, MQTT_CONNECTED_BIT,
                                           pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(ANCHOR_MQTT_CONNECT_TIMEOUT_MS));
    if (!(bits & MQTT_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "MQTT not connected after init — reconnect loop will handle it");
    }

    ESP_LOGI(TAG, "WiFi+MQTT init complete");
}

bool wifi_mqtt_ensure_connected(void)
{
    EventBits_t bits = xEventGroupGetBits(s_net_events);
    if ((bits & WIFI_CONNECTED_BIT) && (bits & MQTT_CONNECTED_BIT)) {
        s_fail_count = 0;
        return true;
    }

    led_status_set(LED_CONNECTING);
    ESP_LOGW(TAG, "Connection lost — reconnecting (attempt %d/5)", s_fail_count + 1);

    bool ok = step_wifi_connect() && step_sntp_sync() && step_mqtt_connect();

    if (ok) {
        s_fail_count = 0;
        led_status_set(LED_CONNECTED);
        ESP_LOGI(TAG, "Reconnected successfully");
        return true;
    }

    s_fail_count++;
    ESP_LOGE(TAG, "Reconnect attempt %d failed", s_fail_count);

    if (s_fail_count >= 5) {
        ESP_LOGE(TAG, "5 consecutive failures — signalling reboot");
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
    return true; /* Still within retry budget — let the main loop call again */
}

void wifi_mqtt_publish_rssi(float rssi)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t ts_ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;

    char buf[96];
    int n = snprintf(buf, sizeof(buf),
                     "{\"anchor_id\":\"%s\",\"rssi\":%.1f,\"ts\":%lld}",
                     ANCHOR_ID, (double)rssi, (long long)ts_ms);

    if (n < 0 || n >= (int)sizeof(buf)) {
        ESP_LOGE(TAG, "publish_rssi: buffer overflow");
        return;
    }

    char topic[48];
    snprintf(topic, sizeof(topic), "indoor/anchor/%s/rssi", ANCHOR_ID);

    esp_mqtt_client_publish(s_mqtt_client, topic, buf, 0, 0, 0);
}

void wifi_mqtt_publish_null_rssi(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t ts_ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;

    char buf[80];
    int n = snprintf(buf, sizeof(buf),
                     "{\"anchor_id\":\"%s\",\"rssi\":null,\"ts\":%lld}",
                     ANCHOR_ID, (long long)ts_ms);

    if (n < 0 || n >= (int)sizeof(buf)) {
        ESP_LOGE(TAG, "publish_null_rssi: buffer overflow");
        return;
    }

    char topic[48];
    snprintf(topic, sizeof(topic), "indoor/anchor/%s/rssi", ANCHOR_ID);

    esp_mqtt_client_publish(s_mqtt_client, topic, buf, 0, 1, 0);
}

void wifi_mqtt_publish_heartbeat(void)
{
    uint64_t uptime_s = (uint64_t)(esp_timer_get_time() / 1000000ULL);
    uint32_t free_heap = esp_get_free_heap_size();

    wifi_ap_record_t ap_info;
    int8_t wifi_rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        wifi_rssi = ap_info.rssi;
    }

    char buf[128];
    int n = snprintf(buf, sizeof(buf),
                     "{\"anchor_id\":\"%s\",\"uptime_s\":%llu,\"wifi_rssi\":%d,\"free_heap\":%lu}",
                     ANCHOR_ID, (unsigned long long)uptime_s,
                     (int)wifi_rssi, (unsigned long)free_heap);

    if (n < 0 || n >= (int)sizeof(buf)) {
        ESP_LOGE(TAG, "publish_heartbeat: buffer overflow");
        return;
    }

    char topic[56];
    snprintf(topic, sizeof(topic), "indoor/anchor/%s/status", ANCHOR_ID);

    esp_mqtt_client_publish(s_mqtt_client, topic, buf, 0, 1, 1);
}
