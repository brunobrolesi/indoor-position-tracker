#pragma once
#include <stdbool.h>

typedef struct {
    bool  tag_detected;   /* true if last packet received within ANCHOR_TAG_TIMEOUT_MS */
    float rssi_filtered;  /* EWA-filtered RSSI (valid only when tag_detected is true) */
} ble_scan_result_t;

/* Init NimBLE and start passive scan from the on_sync callback. */
void ble_scan_init(void);

/* Cancel active scan (called before OTA WiFi init). */
void ble_scan_stop(void);

/* Thread-safe read of the latest scan result. */
void ble_scan_get_result(ble_scan_result_t *out);

/* Reset EWA state so the next packet re-initializes the filter (called after tag loss). */
void ble_scan_reset_ewa(void);
