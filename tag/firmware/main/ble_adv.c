/*
 * BLE Extended Advertising — Eddystone-UID over Coded PHY S=8
 *
 * NimBLE API path confirmed (T0 spike):
 *   nimble_port_init()
 *   → ble_hs_cfg.sync_cb = ble_adv_on_sync
 *   → nimble_port_freertos_init(nimble_host_task)
 *   → on_sync fires → ble_gap_ext_adv_configure() / set_adv_data() / start()
 *
 * BLE_GAP_EVENT_ADV_COMPLETE does NOT fire for non-connectable non-scannable
 * Extended Advertising on ESP32 IDF v6.0 NimBLE. Sequence counter is therefore
 * updated via a periodic esp_timer at BLE_ADV_INTERVAL_MS.
 */

#include "ble_adv.h"
#include "tag_config.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <stdint.h>
#include <string.h>
#include <stdatomic.h>

static const char *TAG = "ble_adv";

/* Advertising instance handle */
#define ADV_INSTANCE 0

/*
 * Eddystone-UID service UUID: 0xFEAA (little-endian on wire: AA FE)
 * Flags: 0x06 = LE General Discoverable + BR/EDR Not Supported
 */
static const uint8_t ADV_FLAGS[]   = {0x02, 0x01, 0x06};
static const uint8_t SVC_UUID[]    = {0x03, 0x03, 0xAA, 0xFE};

/*
 * UUID 019d8ba3-5d5f-792b-8e60-906fbeca324a
 * Namespace = first 10 bytes, Instance = last 6 bytes
 */
static const uint8_t NAMESPACE[10] = {0x01, 0x9D, 0x8B, 0xA3, 0x5D, 0x5F, 0x79, 0x2B, 0x8E, 0x60};
static const uint8_t INSTANCE[6]   = {0x90, 0x6F, 0xBE, 0xCA, 0x32, 0x4A};

/* Sequence counter — single writer (esp_timer callback), atomic for safety */
static volatile _Atomic uint16_t s_seq = 0;

static esp_timer_handle_t s_adv_timer = NULL;
static bool s_adv_running = false;

/* Update advertising data with new sequence counter (advertising keeps running) */
static void adv_send_one(void)
{
    uint16_t seq = atomic_fetch_add(&s_seq, 1);

    /*
     * Eddystone-UID frame = 20 bytes:
     *   [0]     Frame Type (0x00)
     *   [1]     Ranging Data (TX Power at 1m)
     *   [2-11]  Namespace (10 bytes)
     *   [12-17] Instance (6 bytes)
     *   [18-19] Reserved → seq counter uint16 LE
     *
     * Service Data AD element = 1 (len) + 1 (type 0x16) + 2 (uuid) + 20 (frame) = 24 bytes
     * len field = 1 (type) + 2 (uuid) + 20 (frame) = 23
     *
     * Full buf = Flags (3) + UUID16 list (4) + SvcData (24) = 31 bytes (BLE adv payload limit)
     */
    uint8_t svc_data[24];
    svc_data[0] = 23;           /* length: type(1) + uuid(2) + frame(20) */
    svc_data[1] = 0x16;         /* AD type: Service Data - 16-bit UUID */
    svc_data[2] = 0xAA;         /* Eddystone UUID low byte */
    svc_data[3] = 0xFE;         /* Eddystone UUID high byte */
    svc_data[4] = 0x00;         /* Frame Type: UID */
    svc_data[5] = (int8_t)BLE_TX_POWER_1M_DBM;
    memcpy(&svc_data[6],  NAMESPACE, 10); /* [6..15] */
    memcpy(&svc_data[16], INSTANCE,  6);  /* [16..21] */
    svc_data[22] = (uint8_t)(seq & 0xFF);         /* Reserved[0]: seq low byte */
    svc_data[23] = (uint8_t)((seq >> 8) & 0xFF);  /* Reserved[1]: seq high byte */

    /* Build flat buffer: Flags | SvcUUID16 | SvcData */
    uint8_t buf[31];
    memcpy(buf,      ADV_FLAGS, sizeof(ADV_FLAGS));
    memcpy(buf + 3,  SVC_UUID,  sizeof(SVC_UUID));
    memcpy(buf + 7,  svc_data,  sizeof(svc_data));

    struct os_mbuf *data = os_msys_get_pkthdr(sizeof(buf), 0);
    if (!data) {
        ESP_LOGE(TAG, "os_msys_get_pkthdr failed");
        return;
    }
    os_mbuf_append(data, buf, sizeof(buf));

    /* Update data while advertising runs continuously (no stop/start needed) */
    int rc = ble_gap_ext_adv_set_data(ADV_INSTANCE, data);
    if (rc != 0) {
        ESP_LOGE(TAG, "set_adv_data rc=%d", rc);
    }
}

static void adv_timer_cb(void *arg)
{
    (void)arg;
    if (s_adv_running) {
        adv_send_one();
    }
}

static void ble_adv_on_sync(void)
{
    ESP_LOGI(TAG, "NimBLE host synced");

    struct ble_gap_ext_adv_params params;
    memset(&params, 0, sizeof(params));
    params.legacy_pdu      = 0;
    params.connectable     = 0;
    params.scannable       = 0;
    params.own_addr_type   = BLE_OWN_ADDR_PUBLIC;
    params.primary_phy     = BLE_HCI_LE_PHY_CODED;
    params.secondary_phy   = BLE_HCI_LE_PHY_CODED;
    /* S=8 coding preference for maximum range */
    params.tx_power        = BLE_TX_POWER_DBM;
    /* Channels 37, 38, 39 — all three (bitmask 0x07) */
    params.channel_map     = 0x07;

    int rc = ble_gap_ext_adv_configure(ADV_INSTANCE, &params, NULL, NULL, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ext_adv_configure rc=%d", rc);
        return;
    }

    s_adv_running = true;

    /* Set initial advertising data before starting */
    adv_send_one();

    /* Start advertising continuously (duration=0, max_events=0 → infinite) */
    int rc2 = ble_gap_ext_adv_start(ADV_INSTANCE, 0, 0);
    if (rc2 != 0) {
        ESP_LOGE(TAG, "ext_adv_start rc=%d", rc2);
        return;
    }

    /* Timer only updates the sequence counter in the payload — advertising keeps running */
    const esp_timer_create_args_t timer_args = {
        .callback = adv_timer_cb,
        .name     = "ble_adv_timer",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_adv_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_adv_timer, BLE_ADV_INTERVAL_MS * 1000ULL));

    ESP_LOGI(TAG, "BLE advertising started (Coded PHY S=8, interval=%d ms)", BLE_ADV_INTERVAL_MS);
}

static void nimble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_adv_init(void)
{
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = ble_adv_on_sync;
    nimble_port_freertos_init(nimble_host_task);
}

void ble_adv_start(void)
{
    s_adv_running = true;
}

void ble_adv_stop(void)
{
    s_adv_running = false;
    if (s_adv_timer) {
        esp_timer_stop(s_adv_timer);
    }
    ble_gap_ext_adv_stop(ADV_INSTANCE);
    ESP_LOGI(TAG, "BLE advertising stopped");
}
