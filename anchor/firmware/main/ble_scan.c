/*
 * BLE Passive Scanner — Eddystone-UID detection + EWA filter
 *
 * NimBLE async init sequence (confirmed in T0 spike):
 *   nimble_port_init()
 *   → ble_hs_cfg.sync_cb = ble_scan_on_sync
 *   → nimble_port_freertos_init(nimble_host_task)
 *   → on_sync fires → ble_gap_disc() starts passive scan
 *
 * Eddystone-UID namespace (10 bytes): 01 9D 8B A3 5D 5F 79 2B 8E 60
 * Eddystone-UID instance  (6 bytes):  90 6F BE CA 32 4A
 * Service UUID 0xFEAA confirmed in AD type 0x03 (Complete 16-bit UUID list)
 * Service Data confirmed in AD type 0x16 at offset: [0-1] uuid, [2] frame type,
 * [3] ranging data, [4-13] namespace, [14-19] instance, [20-21] reserved/seq.
 *
 * Coexistence sdkconfig flags required (validate in T0):
 *   CONFIG_ESP_COEX_ENABLED=y
 *   CONFIG_SW_COEXIST_ENABLE=y
 *   CONFIG_ESP_COEX_WIFI_DEFAULT_COEX_TYPE=0  (if needed — update before T3)
 */

#include "ble_scan.h"
#include "anchor_config.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdint.h>

static const char *TAG = "ble_scan";

/* Eddystone-UID namespace and instance for the authorized tag */
static const uint8_t NAMESPACE[10] = {0x01, 0x9D, 0x8B, 0xA3, 0x5D, 0x5F, 0x79, 0x2B, 0x8E, 0x60};
static const uint8_t INSTANCE[6]   = {0x90, 0x6F, 0xBE, 0xCA, 0x32, 0x4A};

/* Shared state — protected by s_mutex */
static SemaphoreHandle_t s_mutex;
static float    s_rssi_filtered   = 0.0f;
static bool     s_ewa_initialized = false;
static int64_t  s_last_seen_ms    = 0;

/* --------------------------------------------------------------------------
 * Advertising data parser
 * -------------------------------------------------------------------------- */

/*
 * Walk the AD (Advertising Data) structure and check for:
 *   1. AD type 0x03 containing Eddystone service UUID 0xFEAA
 *   2. AD type 0x16 with 0xFEAA + frame type 0x00 + matching Namespace/Instance
 * Returns true if both are present and the namespace+instance match exactly.
 */
static bool parse_eddystone_uid(const uint8_t *data, uint8_t len)
{
    bool has_feaa_uuid    = false;
    bool has_uid_matching = false;

    uint8_t i = 0;
    while (i < len) {
        uint8_t ad_len  = data[i];
        if (ad_len == 0 || i + ad_len >= len) break;
        uint8_t ad_type = data[i + 1];
        const uint8_t *ad_data = &data[i + 2];
        uint8_t ad_data_len = ad_len - 1;

        if (ad_type == 0x03) {
            /* Complete list of 16-bit UUIDs — look for 0xFEAA (little-endian: AA FE) */
            for (uint8_t j = 0; j + 1 < ad_data_len; j += 2) {
                if (ad_data[j] == 0xAA && ad_data[j + 1] == 0xFE) {
                    has_feaa_uuid = true;
                    break;
                }
            }
        } else if (ad_type == 0x16 && ad_data_len >= 20) {
            /* Service Data — 16-bit UUID: [0-1] uuid, [2] frame type, [3] ranging,
             *                             [4-13] namespace, [14-19] instance */
            if (ad_data[0] == 0xAA && ad_data[1] == 0xFE &&  /* UUID 0xFEAA */
                ad_data[2] == 0x00) {                          /* Frame type UID */
                if (memcmp(&ad_data[4], NAMESPACE, 10) == 0 &&
                    memcmp(&ad_data[14], INSTANCE, 6) == 0) {
                    has_uid_matching = true;
                }
            }
        }

        i += ad_len + 1;
    }

    return has_feaa_uuid && has_uid_matching;
}

/* --------------------------------------------------------------------------
 * NimBLE GAP event callback (runs in NimBLE host task)
 * -------------------------------------------------------------------------- */

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    if (event->type != BLE_GAP_EVENT_DISC) {
        return 0;
    }

    const uint8_t *ad_data = event->disc.data;
    uint8_t ad_len         = event->disc.length_data;

    if (!parse_eddystone_uid(ad_data, ad_len)) {
        return 0;
    }

    float rssi_raw = (float)event->disc.rssi;
    int64_t now_ms = esp_timer_get_time() / 1000;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (!s_ewa_initialized) {
        s_rssi_filtered   = rssi_raw;
        s_ewa_initialized = true;
    } else {
        s_rssi_filtered = ANCHOR_EWA_ALPHA * rssi_raw
                        + (1.0f - ANCHOR_EWA_ALPHA) * s_rssi_filtered;
    }
    s_last_seen_ms = now_ms;

    xSemaphoreGive(s_mutex);

    esp_task_wdt_reset();

    return 0;
}

/* --------------------------------------------------------------------------
 * NimBLE host task
 * -------------------------------------------------------------------------- */

static void nimble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_scan_on_sync(void)
{
    ESP_LOGI(TAG, "NimBLE host synced — starting passive scan");

    struct ble_gap_disc_params scan_params = {
        .itvl              = (ANCHOR_SCAN_INTERVAL_MS * 1000) / 625,
        .window            = (ANCHOR_SCAN_INTERVAL_MS * 1000) / 625,
        .filter_policy     = BLE_HCI_SCAN_FILT_NO_WL,
        .limited           = 0,
        .passive           = 1,   /* passive — no scan requests */
        .filter_duplicates = 0,   /* every packet = new RSSI sample */
    };

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &scan_params,
                          gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: rc=%d", rc);
    }
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void ble_scan_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    configASSERT(s_mutex);

    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = ble_scan_on_sync;
    nimble_port_freertos_init(nimble_host_task);

    /* Register NimBLE host task with TWDT so it doesn't trigger a spurious panic */
    TaskHandle_t nimble_task_handle = xTaskGetHandle("nimble_host");
    if (nimble_task_handle) {
        esp_task_wdt_add(nimble_task_handle);
    }

    ESP_LOGI(TAG, "BLE passive scan init (interval=%d ms, alpha=%.1f)",
             ANCHOR_SCAN_INTERVAL_MS, (double)ANCHOR_EWA_ALPHA);
}

void ble_scan_stop(void)
{
    ble_gap_disc_cancel();
    ESP_LOGI(TAG, "BLE passive scan stopped");
}

void ble_scan_get_result(ble_scan_result_t *out)
{
    int64_t now_ms = esp_timer_get_time() / 1000;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    bool timed_out = (now_ms - s_last_seen_ms) > ANCHOR_TAG_TIMEOUT_MS;
    out->tag_detected  = s_ewa_initialized && !timed_out;
    out->rssi_filtered = s_rssi_filtered;

    xSemaphoreGive(s_mutex);
}

void ble_scan_reset_ewa(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_ewa_initialized = false;
    s_rssi_filtered   = 0.0f;
    xSemaphoreGive(s_mutex);
}
