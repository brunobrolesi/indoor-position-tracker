# Implementation Plan — EPIC-02: Anchor Firmware (MVP)

## Overview

This plan describes how to implement the ESP32-S3 anchor firmware for the indoor positioning MVP. The firmware is written in C using ESP-IDF v6.0 and must: passively scan BLE for Eddystone-UID packets from the authorised tag, apply an on-chip EWA noise filter, and publish filtered RSSI readings to an MQTT broker at ≥ 4 msg/s over WiFi. Supporting features include SNTP time synchronisation for millisecond-resolution timestamps, a reconnection state machine with watchdog-triggered reboot after 5 failures, a multi-state LED driven by a dedicated FreeRTOS task via WS2812/RMT, OTA update over HTTP, and a 10-second heartbeat with retain flag. The project is structured as a single ESP-IDF component project under `anchor/firmware/`, mirroring the tag firmware layout. All per-anchor configuration (ID, position, credentials) is isolated in `anchor_config.h` so that a single firmware base compiles for all three anchors.

The anchor differs architecturally from the tag in three key ways: (1) BLE role is inverted — the anchor is a passive scanner, not an advertiser; (2) WiFi + MQTT are always-on (not OTA-only); and (3) BLE scanning and WiFi operate concurrently, requiring the ESP32-S3 coexistence arbitrator to be active throughout normal operation.

---

## Assumptions & Open Questions

| # | Assumption / Question | Status |
|---|----------------------|--------|
| A1 | ESP32-S3 BLE passive scan + WiFi concurrent operation is supported via the built-in coexistence arbitrator (`CONFIG_ESP_COEX_ENABLED=y`, `CONFIG_SW_COEXIST_ENABLE=y`). RSSI measurement degradation under active WiFi traffic is expected but not characterised. | Open — validate in T0 spike |
| A2 | `ANCHOR_LED_GPIO = GPIO_NUM_48` (WS2812B onboard LED on ESP32-S3-DevKitC-1, same as tag). LED requires the RMT peripheral and the `espressif/led_strip` managed component. | Placeholder — confirm from schematic |
| A3 | `ANCHOR_OTA_TRIGGER_GPIO = GPIO_NUM_0` (BOOT button, same pattern as tag) | Placeholder — confirm from schematic |
| A4 | The MQTT broker accepts username/password authentication without TLS in MVP (`ANCHOR_MQTT_TLS_ENABLED = false`). TLS code path is present behind a compile-time flag but is not tested in MVP. | MVP assumption; TLS tested in production build |
| A5 | `pool.ntp.org` is reachable from the lab LAN. SNTP sync completes within 5 s (`ANCHOR_SNTP_SYNC_TIMEOUT_MS = 5000`). | Lab dependency |
| A6 | The ESP-MQTT IDF component (`esp_mqtt`) is used for the MQTT client. It is a built-in IDF component in ESP-IDF v6.0 — no managed component entry needed in `idf_component.yml`. | Standard IDF component |
| A7 | NimBLE is used for BLE passive scanning (consistent with the tag). The passive scan API (`ble_gap_disc()` with `passive = 1`) is confirmed available in NimBLE on ESP32-S3 with IDF v6.0. | To be validated in T0 |
| A8 | All three anchor units use a 4 MB flash. Partition layout is identical to the tag (`partitions.csv`). | Consistent with tag |
| A9 | `esp_timer`-based 200 ms publish cycle is used in the main loop (not a separate FreeRTOS task) to keep the architecture simple and consistent with the tag. The main task feeds the watchdog every 200 ms — well within the 10 s timeout. | Design decision |
| Q1 | Should the MQTT reconnect state machine live in `wifi_mqtt.c` or be driven from `app_main`? | **Resolved — encapsulated in `wifi_mqtt.c`** with a public `wifi_mqtt_ensure_connected()` call that the main loop invokes before each publish. |
| Q2 | How are three distinct `anchor_config.h` files managed in the repository? | **Resolved — three files per anchor**: `anchor_config_A1.h`, `anchor_config_A2.h`, `anchor_config_A3.h`. At build time, `CMakeLists.txt` includes the file specified by the `ANCHOR_UNIT` CMake variable (default: `A1`). The generic `anchor_config.h` includes the selected file via a generated stub. Flash instructions in `README.md` document the `-DANCHOR_UNIT=A2` idf.py flag. |

---

## Task Breakdown

Tasks are ordered by dependency.

---

### T0 — BLE Passive Scan Spike + WiFi/BLE Coexistence

**Description**
Validate that NimBLE's passive scan API works on ESP32-S3 with IDF v6.0 before any integration work begins. Specifically: (a) passive BLE scan detects Eddystone-UID advertising packets from the tag; (b) RSSI value is accessible in the scan report; (c) concurrent BLE scan + WiFi station mode does not crash or prevent MQTT delivery; (d) UUID filtering can be applied by inspecting the advertising data payload.

This spike resolves A1 and A7 and de-risks T3 and T4.

**Scope**
- A minimal standalone sketch (not integrated into the main project) that starts NimBLE passive scan and logs all discovered BLE devices with RSSI to serial.
- Connect WiFi in STA mode in the same firmware and ping the broker — confirm both run without crash for 60 s.
- Parse one advertising report and confirm the Eddystone namespace bytes are accessible.

**Key API path (NimBLE passive scan)**
```c
// After nimble_port_init() + on_sync callback fires:
struct ble_gap_disc_params scan_params = {
    .itvl             = (ANCHOR_SCAN_INTERVAL_MS * 1000) / 625,  // 0.625 ms units
    .window           = (ANCHOR_SCAN_INTERVAL_MS * 1000) / 625,  // window = interval
    .filter_policy    = BLE_HCI_SCAN_FILT_NO_WL,
    .limited          = 0,
    .passive          = 1,    // passive scan — no scan requests
    .filter_duplicates = 0,   // no dedup; every packet is a new RSSI sample
};
ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &scan_params, gap_event_cb, NULL);
// RSSI is in: event->disc.rssi (int8_t)
// AD data is in: event->disc.data / event->disc.length_data
```

**Required coexistence sdkconfig flags (validate in this spike)**
```
CONFIG_ESP_COEX_ENABLED=y
CONFIG_SW_COEXIST_ENABLE=y
CONFIG_ESP_COEX_WIFI_DEFAULT_COEX_TYPE=0
```

**Acceptance Criteria**
- NimBLE passive scan detects advertising packets from the tag (UUID `019d8ba3-5d5f-792b-8e60-906fbeca324a`).
- `event->disc.rssi` contains a non-zero negative value matching the observed signal strength.
- Advertising data payload contains the Eddystone service data (`0xFEAA` UUID + 17-byte UID frame) and the Namespace bytes `01 9D 8B A3 5D 5F 79 2B 8E 60` are found at the expected offset.
- BLE scan + WiFi STA + MQTT publish run concurrently for 60 s without panic or disconnect.
- Document the confirmed coexistence sdkconfig flags, the NimBLE async `on_sync` sequence, and the exact offset of Namespace bytes within the `BLE_GAP_EVENT_DISC` payload in a comment block at the top of `main/ble_scan.c`.

**Dependencies**
None — run in parallel with T1.

**Testing Notes**
- Power the tag alongside the anchor dev board to generate real advertising traffic.
- If coexistence causes RSSI degradation > 10 dBm compared to BLE-only mode, record in the spike notes — it does not block T3 but must be mentioned in `validation.md`.

**Post-Spike Action (before T3 begins)**
Compare the confirmed coexistence flags against T1's `sdkconfig.defaults`. If any flag differs (including `CONFIG_ESP_COEX_WIFI_DEFAULT_COEX_TYPE=0`), update `anchor/firmware/sdkconfig.defaults` in a follow-up commit before T3 starts. T1 and T0 run in parallel so T1 may land first — the `sdkconfig.defaults` patch is acceptable as a follow-up, but it must land before T3 is implemented.

---

### T1 — Project Scaffold & Build System

**Description**
Create the ESP-IDF project skeleton under `anchor/firmware/`. Mirrors the tag layout. Includes `CMakeLists.txt`, `sdkconfig.defaults`, the `main/` component, `idf_component.yml`, and the `anchor_config.h` stub mechanism for all three anchor units.

**Directory layout**
```
anchor/firmware/
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv           (T2)
├── idf_component.yml
├── main/
│   ├── CMakeLists.txt
│   ├── app_main.c
│   ├── anchor_config.h      (generated stub — #includes the unit-specific file)
│   ├── anchor_config_A1.h
│   ├── anchor_config_A2.h
│   ├── anchor_config_A3.h
│   ├── ble_scan.c / .h      (T3)
│   ├── wifi_mqtt.c / .h     (T4 + T5)
│   ├── led_status.c / .h    (T6)
│   └── ota_update.c / .h    (T7)
```

**CMakeLists.txt multi-anchor mechanism**
```cmake
cmake_minimum_required(VERSION 3.16)
# Default to A1 if not specified via -DANCHOR_UNIT=A2
if(NOT DEFINED ANCHOR_UNIT)
    set(ANCHOR_UNIT "A1")
endif()
add_compile_definitions(ANCHOR_UNIT_FILE="anchor_config_${ANCHOR_UNIT}.h")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(anchor_firmware)
```

`anchor_config.h` stub:
```c
#pragma once
#include ANCHOR_UNIT_FILE
```

**`anchor_config_A1.h` stub (placeholder values)**

All constants listed in the spec's `anchor_config.h` reference table must be present. Omitting any one is a compile error (enforced by `static_assert` for `ANCHOR_EWA_ALPHA`).

```c
#pragma once
#include "driver/gpio.h"

#define ANCHOR_ID                      "A1"
#define ANCHOR_POS_X                   0.0f
#define ANCHOR_POS_Y                   0.0f
#define ANCHOR_WIFI_SSID               "CHANGE_ME"
#define ANCHOR_WIFI_PASSWORD           "CHANGE_ME"
#define ANCHOR_MQTT_HOST               "192.168.1.100"
#define ANCHOR_MQTT_PORT               1883
#define ANCHOR_MQTT_USER               "CHANGE_ME"
#define ANCHOR_MQTT_PASSWORD           "CHANGE_ME"
#define ANCHOR_MQTT_TLS_ENABLED        false
#define ANCHOR_MQTT_CLIENT_ID          "anchor_A1"
#define ANCHOR_NTP_SERVER              "pool.ntp.org"
#define ANCHOR_OTA_SERVER_URL          "http://192.168.1.100:8000/anchor.bin"
#define ANCHOR_OTA_TRIGGER_GPIO        GPIO_NUM_0
#define ANCHOR_SCAN_INTERVAL_MS        200
#define ANCHOR_EWA_ALPHA               0.3f
_Static_assert(ANCHOR_EWA_ALPHA >= 0.1f && ANCHOR_EWA_ALPHA <= 1.0f, \
               "ANCHOR_EWA_ALPHA must be in [0.1, 1.0]");
#define ANCHOR_WIFI_CONNECT_TIMEOUT_MS     10000
#define ANCHOR_OTA_WIFI_CONNECT_TIMEOUT_MS 30000  // deliberate: RF-08b requires 30 s for OTA
#define ANCHOR_SNTP_SYNC_TIMEOUT_MS        5000
#define ANCHOR_MQTT_CONNECT_TIMEOUT_MS     5000
#define ANCHOR_TAG_TIMEOUT_MS          2000
#define ANCHOR_HEARTBEAT_INTERVAL_S    10
#define ANCHOR_WDT_TIMEOUT_SEC         10
#define ANCHOR_LED_GPIO                GPIO_NUM_48
```

Duplicate the stub for `anchor_config_A2.h` and `anchor_config_A3.h` with `ANCHOR_ID` and `ANCHOR_MQTT_CLIENT_ID` updated accordingly. `ANCHOR_POS_X`/`Y` remain `0.0f` until surveyed.

> **LED GPIO note:** The spec's constants reference table lists `ANCHOR_LED_GPIO` placeholder as `GPIO_NUM_2`, but the spec's Hardware Placeholders section and the DevKitC-1 schematic specify GPIO 48 (onboard WS2812). Use `GPIO_NUM_48` in all three config stubs. Flag this inconsistency in the spec for correction.

**`sdkconfig.defaults`**
```
# Bluetooth — NimBLE only
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=n
CONFIG_BT_CONTROLLER_ENABLED=y
CONFIG_BT_CLASSIC_ENABLED=n

# NimBLE passive scan (no Extended Advertising needed)
CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT=12
CONFIG_BT_NIMBLE_HS_TASK_STACK_SIZE=4096

# WiFi + BLE coexistence (validate flags in T0)
CONFIG_ESP_COEX_ENABLED=y
CONFIG_SW_COEXIST_ENABLE=y

# WiFi
CONFIG_ESP_WIFI_ENABLED=y
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=10
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=32

# SNTP
CONFIG_LWIP_DHCP_GET_NTP_SRV=y

# NVS
CONFIG_NVS_ENCRYPTION=n

# Flash / OTA
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="4MB"
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# Task watchdog
CONFIG_ESP_TASK_WDT_INIT=y
CONFIG_ESP_TASK_WDT_PANIC=y

# Log
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
```

Note: `CONFIG_BT_NIMBLE_EXT_ADV` is **not** needed — the anchor only scans, it does not use Extended Advertising.

**`idf_component.yml`**
```yaml
dependencies:
  espressif/led_strip: ">=2.0.0"
```

**Acceptance Criteria**
- `idf.py -DANCHOR_UNIT=A1 build` succeeds with an empty `app_main()`.
- `idf.py -DANCHOR_UNIT=A2 build` succeeds and uses `anchor_config_A2.h` (verify by checking `ANCHOR_ID` in the binary strings).
- `static_assert` on `ANCHOR_EWA_ALPHA` fires a compile error when set to `0.0f` or `2.0f`.
- All constants from the spec's reference table are present in the stub files.

**Dependencies**
None — first task. Run in parallel with T0.

**Testing Notes**
- Confirm that `idf.py menuconfig` shows the expected sdkconfig defaults applied.
- Grep for `ANCHOR_EWA_ALPHA` usage after T3 to confirm the constant propagates.

---

### T2 — Partition Table (Safe OTA)

**Description**
Create `anchor/firmware/partitions.csv`. Identical layout to the tag partition table: NVS + OTA data + phy_init + two equal OTA app partitions. The filename `partitions.csv` matches the spec's DoD wording; `sdkconfig.defaults` references it via `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"`.

**Layout (4 MB flash — identical to tag)**
```
# Name,    Type, SubType,  Offset,   Size
nvs,       data, nvs,      0x9000,   0x6000
otadata,   data, ota,      0xf000,   0x2000
phy_init,  data, phy,      0x11000,  0x1000
ota_0,     app,  ota_0,    0x20000,  0x180000
ota_1,     app,  ota_1,    0x1A0000, 0x180000
```

**Acceptance Criteria**
- `idf.py partition-table` shows the correct layout without warnings.
- Both `ota_0` and `ota_1` are 1.5 MB each.
- The anchor firmware binary fits within 1.5 MB (verify after T8 full build).

**Inputs / Outputs**
- Output: `anchor/firmware/partitions.csv`

**Dependencies**
T1 (project scaffold must exist).

**Testing Notes**
- Flash and verify with `esp_ota_ops` API returning active partition without error in `idf.py monitor`.

---

### T3 — BLE Passive Scanner + EWA Filter (RF-01, RF-02, RF-09)

**Description**
Implement the BLE scan module (`ble_scan.c` / `ble_scan.h`). This module initialises NimBLE, starts a continuous passive scan, filters advertising reports for the authorised Eddystone-UID tag, applies the EWA filter to the raw RSSI, and exposes the latest filtered value to the main loop. It also tracks the tag-last-seen timestamp to implement the tag timeout logic (RF-09).

**NimBLE async init sequence (confirmed in T0)**
```
1. nimble_port_init()
2. ble_hs_cfg.sync_cb = ble_scan_on_sync   // register callback
3. nimble_port_freertos_init(nimble_host_task)
4. ble_scan_on_sync() fires → calls ble_gap_disc() to start scanning
```
No `ble_gap_*` call may be made before `on_sync` fires.

**Eddystone-UID filtering in the advertising data**
When a `BLE_GAP_EVENT_DISC` event arrives, parse `event->disc.data` (length in `event->disc.length_data`) to find:
1. An AD element with type `0x03` (Complete List of 16-bit UUIDs) containing `0xFEAA` (Eddystone service UUID, little-endian: `0xAA 0xFE`).
2. An AD element with type `0x16` (Service Data — 16-bit UUID) starting with `0xAA 0xFE` followed by frame type `0x00` (UID), then the Namespace and Instance bytes.

Namespace to match (10 bytes): `01 9D 8B A3 5D 5F 79 2B 8E 60`
Instance to match (6 bytes):   `90 6F BE CA 32 4A`

Only process the RSSI if both Namespace and Instance match exactly. The 2-byte Reserved/counter field that follows must be ignored (per spec: no action required on it, rollover is not an error).

**EWA filter (RF-02)**
```c
// State (static in ble_scan.c):
static float s_rssi_filtered = 0.0f;
static bool  s_ewa_initialized = false;

// On each matching packet:
float rssi_raw = (float)event->disc.rssi;
if (!s_ewa_initialized) {
    s_rssi_filtered = rssi_raw;   // first reading or post-loss reset
    s_ewa_initialized = true;
} else {
    s_rssi_filtered = ANCHOR_EWA_ALPHA * rssi_raw
                    + (1.0f - ANCHOR_EWA_ALPHA) * s_rssi_filtered;
}
s_last_seen_ms = esp_timer_get_time() / 1000;
```

When the tag is not seen for `ANCHOR_TAG_TIMEOUT_MS` (RF-09), the main loop detects this and publishes null-rssi. When the tag returns, `s_ewa_initialized` must be reset to `false` so the next packet re-initializes the EWA state.

**Concurrency**
The NimBLE GAP event callback runs inside the NimBLE host task. The main loop reads `s_rssi_filtered` and `s_last_seen_ms` from a different task. Protect shared state with a FreeRTOS mutex (`SemaphoreHandle_t`). Acquire the mutex in both the GAP event callback (writer) and `ble_scan_get_result()` / `ble_scan_reset_ewa()` (readers). On a dual-core ESP32-S3, `volatile` alone does not provide the memory-ordering guarantees needed for multi-field reads — use a mutex consistently throughout the module.

**Task Watchdog registration**
After `nimble_port_freertos_init()` returns the host task handle, register it with the TWDT:
```c
TaskHandle_t nimble_task_handle = xTaskGetHandle("nimble_host");
if (nimble_task_handle) {
    esp_task_wdt_add(nimble_task_handle);
}
```
The NimBLE host task must call `esp_task_wdt_reset()` periodically inside its processing loop, or the GAP event callback must reset it on each received packet. Confirm the chosen approach doesn't introduce a WDT timeout during low-BLE-traffic periods.

**Public API**
```c
// ble_scan.h
void  ble_scan_init(void);  // nimble_port_init → register on_sync → nimble_port_freertos_init
                            // Scanning starts automatically from on_sync callback.

typedef struct {
    bool  tag_detected;      // true if last packet was within ANCHOR_TAG_TIMEOUT_MS
    float rssi_filtered;     // EWA-filtered RSSI (valid only if tag_detected)
} ble_scan_result_t;

void ble_scan_get_result(ble_scan_result_t *out);  // thread-safe read
void ble_scan_reset_ewa(void);                     // reset EWA state (called by main after tag loss)
```

**Acceptance Criteria**
- Passive scan detects the tag's advertising packets; `ble_scan_get_result()` returns `tag_detected = true`.
- `rssi_filtered` converges toward the raw RSSI within ~10 packets (EWA with α=0.3 reaches ~95% in ~10 steps).
- After `ANCHOR_TAG_TIMEOUT_MS` with no packets, `tag_detected` flips to `false`.
- When the tag reappears, `ble_scan_reset_ewa()` is called and the first new packet re-initializes the filter (no stale state from the previous session).
- No stack overflow or mutex deadlock observed in 60 s of `idf.py monitor`.
- Filter does not apply Active scan (no scan request packets visible in a BLE sniffer).

**Inputs / Outputs**
- Input: `anchor_config.h` constants (`ANCHOR_SCAN_INTERVAL_MS`, `ANCHOR_EWA_ALPHA`, `ANCHOR_TAG_TIMEOUT_MS`).
- Output: `main/ble_scan.c`, `main/ble_scan.h`.

**Dependencies**
T0 (BLE passive scan spike validated), T1.

**Testing Notes**
- Power off the tag for 3 s; verify `tag_detected` flips to `false` after `ANCHOR_TAG_TIMEOUT_MS = 2000 ms`.
- Power on the tag again; verify `tag_detected` flips to `true` within one scan interval.
- Log `rssi_raw` vs `rssi_filtered` to serial for 30 s in a static environment; verify visually that the filtered signal is smoother.
- Unit test (host-side, if possible): feed a synthetic sequence of RSSI values into the EWA logic and verify the output matches hand-calculated values.

---

### T4 — WiFi + SNTP Connection Manager (RF-05, prerequisite for T5)

**Description**
Implement the WiFi connection and SNTP synchronisation layer in `wifi_mqtt.c` (shared file with T5). This subtask covers everything up to and including SNTP sync, which must complete before any MQTT timestamp is valid.

**WiFi init and connection**
```c
esp_netif_init();
esp_event_loop_create_default();
esp_netif_create_default_wifi_sta();
esp_wifi_init(&wifi_init_cfg);
esp_wifi_set_mode(WIFI_MODE_STA);
esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);  // SSID/password from anchor_config.h
esp_wifi_start();
esp_wifi_connect();
```
Wait for `WIFI_EVENT_STA_CONNECTED` + `IP_EVENT_STA_GOT_IP` using a FreeRTOS event group. Timeout: `ANCHOR_WIFI_CONNECT_TIMEOUT_MS`.

**SNTP sync**
After WiFi IP assignment:
```c
esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
esp_sntp_setservername(0, ANCHOR_NTP_SERVER);
esp_sntp_init();
// Poll until sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED
// Timeout: ANCHOR_SNTP_SYNC_TIMEOUT_MS
```
After SNTP sync, `gettimeofday()` returns Unix time with millisecond resolution.

**Reconnect state machine (RF-05)**
```
[step 1] esp_wifi_connect()         — timeout: ANCHOR_WIFI_CONNECT_TIMEOUT_MS
[step 2] SNTP re-sync               — timeout: ANCHOR_SNTP_SYNC_TIMEOUT_MS
[step 3] esp_mqtt_client_reconnect() — timeout: ANCHOR_MQTT_CONNECT_TIMEOUT_MS

Retry interval: 1 s fixed backoff between full attempts
Max attempts: 5 consecutive failures → esp_restart() (watchdog-triggered reboot)
Success: reset attempt counter to 0
```
The reconnect logic is encapsulated in a single function `wifi_mqtt_ensure_connected()` called from the main loop before each publish. It is a no-op when both WiFi and MQTT are connected. If either is down, it runs the reconnect sequence.

**Acceptance Criteria**
- WiFi connects to the lab AP within `ANCHOR_WIFI_CONNECT_TIMEOUT_MS`.
- `gettimeofday()` returns a plausible Unix epoch in milliseconds after SNTP sync.
- Pulling the Ethernet cable and re-plugging it triggers automatic reconnection within ~5 s (5 × 1 s backoff).
- After 5 consecutive failures, the device reboots (verify with `idf.py monitor`).
- A successful reconnect resets the failure counter (confirm with: disconnect → reconnect → disconnect → reconnect → ... → 4 times in a row without 5 consecutive).
- SNTP timeout during a reconnect attempt counts the full attempt as failed. Publishing with a stale clock is not acceptable; the firmware does not publish while any reconnect step is pending. This means the 4 msg/s guarantee is suspended during active reconnection — which is acceptable because RF-03 applies to "normal operating conditions."

**Inputs / Outputs**
- Input: `ANCHOR_WIFI_SSID`, `ANCHOR_WIFI_PASSWORD`, `ANCHOR_NTP_SERVER`, all timeout constants.
- Output: first half of `main/wifi_mqtt.c`, `main/wifi_mqtt.h` (WiFi + SNTP API).

**Dependencies**
T1 (project scaffold), T0 (coexistence confirmed).

**Testing Notes**
- Simulate WiFi loss by disabling the AP in the router admin panel; time the reconnect.
- Verify SNTP by checking that the `ts` field in a published message matches the wall clock within ±1 s.

---

### T5 — MQTT Client + Publish API (RF-03, RF-04, RF-07, RF-09)

**Description**
Add the MQTT client initialisation and publish functions to `wifi_mqtt.c`. This builds on the WiFi+SNTP layer from T4. The MQTT publish API is called by the main loop at the 200 ms scan cadence.

**MQTT client init**
```c
esp_mqtt_client_config_t mqtt_cfg = {
    .broker.address.hostname = ANCHOR_MQTT_HOST,
    .broker.address.port     = ANCHOR_MQTT_PORT,
    .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,  // TCP for MVP
    .credentials.username    = ANCHOR_MQTT_USER,
    .credentials.authentication.password = ANCHOR_MQTT_PASSWORD,
    .credentials.client_id   = ANCHOR_MQTT_CLIENT_ID,
};
// If ANCHOR_MQTT_TLS_ENABLED:
//   mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
esp_mqtt_client_start(s_mqtt_client);
```

**RSSI publish (RF-03, RF-04) — QoS 0, no retain**
```c
// Topic: indoor/anchor/{ANCHOR_ID}/rssi
// Payload: {"anchor_id":"A1","rssi":-67.3,"ts":1712345678123}
// rssi: one decimal place; ts: Unix epoch in milliseconds
void wifi_mqtt_publish_rssi(float rssi_filtered);
```
Format using `snprintf` into a stack-allocated buffer (~80 bytes). Timestamp via:
```c
struct timeval tv;
gettimeofday(&tv, NULL);
int64_t ts_ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
```

**Null-rssi publish (RF-09) — QoS 1, no retain**
```c
// Payload: {"anchor_id":"A1","rssi":null,"ts":1712345678123}
// rssi field is JSON null — not "null" string, not -9999 sentinel.
void wifi_mqtt_publish_null_rssi(void);
```

**Heartbeat publish (RF-07) — QoS 1, retain**
```c
// Topic: indoor/anchor/{ANCHOR_ID}/status
// Payload: {"anchor_id":"A1","uptime_s":3600,"wifi_rssi":-55,"free_heap":180000}
// wifi_rssi from: wifi_ap_record_t ap_info; esp_wifi_sta_get_ap_info(&ap_info); ap_info.rssi
// free_heap from: esp_get_free_heap_size()
// uptime_s: esp_timer_get_time() / 1000000
void wifi_mqtt_publish_heartbeat(void);
```
Called from the main loop every `ANCHOR_HEARTBEAT_INTERVAL_S` seconds.

**MQTT retain + QoS matrix**

| Topic | QoS | Retain | Rationale |
|-------|-----|--------|-----------|
| `indoor/anchor/{id}/rssi` (normal) | 0 | No | High-frequency; packet loss acceptable |
| `indoor/anchor/{id}/rssi` (null) | 1 | No | Semantically important signal-loss event |
| `indoor/anchor/{id}/status` | 1 | Yes | New subscriber gets last health state immediately |

**Acceptance Criteria**
- MQTT Explorer (or `mosquitto_sub`) shows well-formed JSON on `indoor/anchor/A1/rssi` at ≥ 4 msg/s.
- `ts` field is a plausible Unix millisecond epoch (not 0, not second-resolution).
- `rssi` field is a float with one decimal place (e.g., `-67.3`), not an integer.
- When tag is absent for >2 s, the `rssi` field is JSON `null` (not `"null"`, not `-9999`).
- Heartbeat message appears on `indoor/anchor/A1/status` every ~10 s with incrementing `uptime_s`.
- New MQTT subscriber immediately receives the last heartbeat (retain flag confirmed).
- QoS 1 null-rssi and heartbeat messages show delivery acknowledgement in `idf.py monitor`.

**Inputs / Outputs**
- Input: all `ANCHOR_MQTT_*` constants; WiFi + SNTP layer from T4.
- Output: second half of `main/wifi_mqtt.c` + complete `main/wifi_mqtt.h`.

**Public API summary**
```c
void wifi_mqtt_init(void);                  // init WiFi + SNTP + MQTT client
bool wifi_mqtt_ensure_connected(void);      // reconnect if needed; returns false if all retries exhausted
void wifi_mqtt_publish_rssi(float rssi);    // QoS 0
void wifi_mqtt_publish_null_rssi(void);     // QoS 1
void wifi_mqtt_publish_heartbeat(void);     // QoS 1 + retain
```

**Dependencies**
T4 (WiFi + SNTP layer), T1.

**Testing Notes**
- Set `ANCHOR_HEARTBEAT_INTERVAL_S = 3` temporarily to accelerate heartbeat testing.
- Verify null-rssi by powering off the tag for 5 s with `mosquitto_sub -v` running.

---

### T6 — LED State Machine (RF-11)

**Description**
Implement a multi-state LED driver (`led_status.c` / `led_status.h`) using a dedicated FreeRTOS task. The LED is a WS2812B on `ANCHOR_LED_GPIO` (GPIO 48 on DevKitC-1), driven via the RMT peripheral using the `espressif/led_strip` managed component — same hardware path as the tag.

The anchor LED has four states (more complex than the tag's single 1 Hz blink):

| State | Pattern | When |
|-------|---------|------|
| `LED_CONNECTING` | Slow blink: 500 ms ON / 500 ms OFF | WiFi or MQTT not yet connected |
| `LED_CONNECTED` | Solid ON | WiFi connected + MQTT CONNACK received |
| `LED_OTA` | Fast blink: 100 ms ON / 100 ms OFF | OTA firmware download in progress |
| `LED_REBOOT` | 3 rapid blinks (100 ms on/off × 3) then OFF | Before controlled reboot after 5 failed reconnects |

**Implementation approach**
A dedicated FreeRTOS task (`led_task`) runs an infinite loop. The current state is stored in a `volatile led_state_t` variable written atomically. The task checks the state and drives the WS2812 accordingly.

**Task Watchdog registration**
Inside `led_status_start()`, after `xTaskCreate`, register the returned task handle with the TWDT:
```c
esp_task_wdt_add(led_task_handle);
```
The `led_task` loop must call `esp_task_wdt_reset()` at the top of every iteration to prevent a WDT timeout during long blink-off intervals (the longest is 500 ms — well within the 10 s window).

```c
typedef enum {
    LED_CONNECTING,
    LED_CONNECTED,
    LED_OTA,
    LED_REBOOT,
} led_state_t;

void led_status_init(void);                // init RMT + led_strip handle
void led_status_start(void);              // create led_task
void led_status_set(led_state_t state);   // called from any task to change state
```

`LED_REBOOT` is a one-shot sequence: after 3 blinks, the task turns the LED off and blocks — the caller is responsible for calling `esp_restart()` immediately after `led_status_set(LED_REBOOT)` plus a short delay (`vTaskDelay(pdMS_TO_TICKS(700))` to allow the blinks to complete).

Per spec RF-11: "This blink is implemented in a dedicated LED task; it will not occur if the device is reset directly by the Task Watchdog due to a hung task." The `LED_REBOOT` sequence only appears on controlled reboots (e.g., after 5 failed reconnect attempts).

**WS2812 colour mapping (optional, for visual clarity)**
- `LED_CONNECTING`: amber (R=255, G=80, B=0)
- `LED_CONNECTED`: green (R=0, G=200, B=0)
- `LED_OTA`: blue (R=0, G=0, B=255)
- `LED_REBOOT`: red (R=255, G=0, B=0)

If a monochrome external LED is used instead of WS2812, the `led_strip` calls reduce to plain GPIO toggle — note this in `anchor_config.h` hardware section.

**Acceptance Criteria**
- `LED_CONNECTING` blinks at 500 ms confirmed visually during WiFi/MQTT setup.
- `LED_CONNECTED` is solid after MQTT CONNACK (verify transition is driven by MQTT event handler, not a timeout).
- `LED_OTA` blinks at 100 ms during OTA download (set by `ota_update.c`).
- `LED_REBOOT` fires exactly 3 blinks before reboot in the controlled-restart path.
- No LED glitch (extra blink, wrong state) during normal WiFi reconnect cycle.

**Inputs / Outputs**
- Input: `ANCHOR_LED_GPIO` constant; `espressif/led_strip` component.
- Output: `main/led_status.c`, `main/led_status.h`.

**Dependencies**
T1.

**Testing Notes**
- Force `led_status_set(LED_REBOOT)` manually from a test command in `idf.py monitor` console (or temporary code) and count the blinks.
- Ensure the `led_task` stack does not overflow: set task stack to 2048 bytes minimum; check with `uxTaskGetStackHighWaterMark()`.

---

### T7 — OTA Update via HTTP (RF-08, RF-08a to RF-08d)

**Description**
Implement OTA mode (`ota_update.c` / `ota_update.h`). Identical pattern to the tag's OTA module, adapted for the anchor: suspend BLE *scanning* (not advertising) before WiFi init, use anchor config constants, and set the LED to `LED_OTA` during download.

**State machine (mirrors tag T7)**
```
Boot
 └─ [Step A — always] esp_ota_mark_app_valid_cancel_rollback()
 └─ Configure ANCHOR_OTA_TRIGGER_GPIO with internal pull-up
 └─ GPIO LOW?
       Yes → OTA Mode
             ├─ led_status_set(LED_OTA)
             ├─ ble_scan_stop()          // suspend scanning before WiFi init
             ├─ Init WiFi STA (reuse ANCHOR_WIFI_SSID / ANCHOR_WIFI_PASSWORD)
             ├─ Connect (ANCHOR_OTA_WIFI_CONNECT_TIMEOUT_MS = 30 000 ms) ──fail──> esp_restart()
             //   Note: RF-08b requires 30 s for OTA, deliberately different from
             //   ANCHOR_WIFI_CONNECT_TIMEOUT_MS (10 s) used for normal operation reconnects.
             ├─ HTTP GET firmware from ANCHOR_OTA_SERVER_URL (60 s timeout)
             │    ──fail──> esp_wifi_stop() + esp_restart()
             ├─ esp_ota_write() stream → esp_ota_end() → esp_ota_set_boot_partition()
             ├─ esp_wifi_stop() + esp_wifi_deinit()
             └─ esp_restart() → new firmware boots → Step A commits it
       No  → return (normal operation)
```

**Note on BLE + WiFi during OTA**
Unlike the tag (which had BLE advertising to stop + WiFi only during OTA), the anchor *already* uses WiFi for MQTT in normal operation. During OTA, the existing MQTT WiFi connection is torn down and a fresh STA init is performed to avoid state machine conflicts. Call `esp_mqtt_client_stop()` before WiFi re-init if MQTT was already connected.

**Public API**
```c
void ota_check_and_run(void);  // returns if GPIO not triggered; does not return if OTA runs
```

**BLE scan stop API (add to ble_scan.h)**
```c
void ble_scan_stop(void);  // calls ble_gap_disc_cancel()
```

**Acceptance Criteria**
- OTA mode is entered when `ANCHOR_OTA_TRIGGER_GPIO` is held LOW at boot (internal pull-up enabled).
- LED shows `LED_OTA` (fast 100 ms blink) during download.
- Successful OTA: device reboots into new firmware; MQTT publications resume normally.
- Rollback: interrupting the HTTP download causes reboot into previous firmware.
- Rollback persistence: after successful OTA, power-cycle the device — it stays on the new firmware (Step A committed it on the first post-OTA boot).
- WiFi is cleanly stopped before reboot on both success and failure paths.

**Inputs / Outputs**
- Input: `ANCHOR_OTA_*` constants; `ble_scan_stop()` from T3; `led_status_set()` from T6.
- Output: `main/ota_update.c`, `main/ota_update.h`.

**Dependencies**
T1, T2 (OTA partitions), T3 (`ble_scan_stop()`), T6 (`led_status_set()`).

**Testing Notes**
- Serve firmware with `python3 -m http.server 8000` in `anchor/firmware/build/`.
- Same test matrix as tag T7: happy path, interrupted download, WiFi failure, rollback persistence.

---

### T8 — Integration: `app_main()` + Scan-Publish Loop

**Description**
Wire all modules in `app_main()` and implement the 200 ms scan-publish cycle that is the anchor's core runtime behavior.

**Boot sequence**
```
1.  nvs_flash_init()                              // Required before BT and NVS
2.  esp_ota_mark_app_valid_cancel_rollback()       // Commit firmware on post-OTA boot; no-op on cold boot
3.  esp_task_wdt_reconfigure({.timeout_ms = ANCHOR_WDT_TIMEOUT_SEC * 1000, .trigger_panic = true})
    esp_task_wdt_add(NULL)                        // Register main task with TWDT
    // nimble_host_task is registered inside ble_scan_init() after nimble_port_freertos_init() (T3)
    // led_task is registered inside led_status_start() after xTaskCreate() (T6)
4.  led_status_init()
    led_status_start()
    led_status_set(LED_CONNECTING)                // LED: slow blink while connecting
5.  ota_check_and_run()                           // May not return if OTA triggered
6.  wifi_mqtt_init()                              // WiFi → SNTP → MQTT (blocks until connected or reboots)
    led_status_set(LED_CONNECTED)                 // LED: solid after MQTT CONNACK
7.  ble_scan_init()                               // NimBLE init → passive scan starts from on_sync callback
8.  // Main scan-publish loop (see below)
```

**Main scan-publish loop (200 ms cadence)**
```c
int64_t last_heartbeat_ms = 0;

while (true) {
    esp_task_wdt_reset();

    // Reconnect if needed (WiFi drop).
    // wifi_mqtt_ensure_connected() is blocking during active reconnect — the 200 ms cadence is
    // suspended while reconnecting. This is acceptable: RF-03's ≥ 4 msg/s guarantee applies to
    // "normal operating conditions" only. No messages are published until reconnect succeeds.
    if (!wifi_mqtt_ensure_connected()) {
        // All 5 retries exhausted — controlled reboot
        led_status_set(LED_REBOOT);
        vTaskDelay(pdMS_TO_TICKS(700));  // allow 3-blink sequence
        esp_restart();
    }

    // Get BLE scan result (thread-safe)
    ble_scan_result_t result;
    ble_scan_get_result(&result);

    if (result.tag_detected) {
        wifi_mqtt_publish_rssi(result.rssi_filtered);
    } else {
        wifi_mqtt_publish_null_rssi();
        ble_scan_reset_ewa();  // reset EWA state for next detection
    }

    // Heartbeat every ANCHOR_HEARTBEAT_INTERVAL_S seconds
    int64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms - last_heartbeat_ms >= (int64_t)ANCHOR_HEARTBEAT_INTERVAL_S * 1000) {
        wifi_mqtt_publish_heartbeat();
        last_heartbeat_ms = now_ms;
    }

    vTaskDelay(pdMS_TO_TICKS(ANCHOR_SCAN_INTERVAL_MS));
}
```

**LED state transitions driven from the main loop**
- `LED_CONNECTING` → set in step 4, cleared to `LED_CONNECTED` after MQTT CONNACK in step 6.
- `LED_CONNECTED` → remains solid during normal operation.
- `LED_CONNECTING` → re-set by `wifi_mqtt_ensure_connected()` internally while reconnecting.
- `LED_REBOOT` → set in the main loop before `esp_restart()` after 5 failed reconnects.
- `LED_OTA` → set by `ota_update.c` before download begins.

**Acceptance Criteria**
- Full boot to first MQTT publish < 10 s (WiFi + SNTP + MQTT connect + first scan cycle).
- `idf.py monitor` shows each init step completing without `ESP_ERR_*` errors.
- Publish rate confirmed ≥ 4 msg/s by counting messages in MQTT Explorer over 5 minutes.
- No task stack overflow warnings.
- LED transitions correctly across boot, reconnect, and (simulated) reboot states.
- All three anchors (A1, A2, A3) operate simultaneously without MQTT client ID conflicts (each has a unique `ANCHOR_MQTT_CLIENT_ID`).

**Dependencies**
T3, T4+T5, T6, T7.

**Testing Notes**
- Run all three anchor DevKits simultaneously; confirm three distinct `anchor_id` values in the broker.
- Measure publish frequency: `mosquitto_sub -t 'indoor/anchor/A1/rssi' | pv -l -i 5` → expect ≥ 20 msgs/5 s.

---

### T9 — Validation & Documentation

**Description**
Run the Definition of Done checklist from the spec, record measurements, and publish documentation.

**Deliverables**
1. `anchor/specs/001-mvp/validation.md` — records with the specific methodology used:
   - EWA validation: raw vs. filtered RSSI standard deviation in a static environment (tag stationary, 5 minutes of data). Target: filtered std dev ≤ 70% of raw std dev (≥ 30% reduction). Methodology: log both values to serial at 5 Hz for 5 min; export CSV; compute std dev in Python/spreadsheet.
   - Publish latency: log delta between `s_last_seen_ms` (BLE callback timestamp) and the `gettimeofday()` call inside `wifi_mqtt_publish_rssi()`; collect 100 samples; report 95th percentile. Target: < 50 ms.
   - Surveyed (x, y) coordinates for all 3 anchors in meters, mapped on the environment floor plan.
   - Physical placement: confirm each anchor is mounted at 2–2.5 m height with no metal obstacles within 30 cm radius; photograph and annotate the floor plan.
   - Publish frequency measurement: msg/s over 5 continuous minutes per anchor.
   - Null-rssi validation: tag powered off for 5 s; confirm `rssi: null` (JSON null) appears in broker after `ANCHOR_TAG_TIMEOUT_MS`.
   - Resilience test: WiFi drop and recovery time for all 3 anchors.
2. `anchor/firmware/README.md` — includes:
   - Prerequisites (ESP-IDF v6.0, toolchain, MQTT broker setup)
   - How to configure per-anchor `anchor_config_A1.h` (credentials, coordinates)
   - Build command: `idf.py -DANCHOR_UNIT=A1 build && idf.py flash monitor`
   - How to set up the OTA HTTP server
   - How to verify with MQTT Explorer
3. Git tag `anchor-firmware-v1.0` on the delivery commit.

**Acceptance Criteria**
- All DoD checkboxes in `spec.md` checked.
- `validation.md` exists with EWA measurements showing ≥ 30% std dev reduction.
- MQTT publications verified for A1, A2, A3: valid JSON, correct `anchor_id`, millisecond `ts`, float `rssi` with 1 decimal.
- Null-rssi is confirmed JSON `null` (not `"null"` or `-9999`) via broker log.
- Publish latency < 50 ms (95th percentile) measured and documented in `validation.md`.
- Physical placement constraints confirmed for all 3 anchors (2–2.5 m height, no metal within 30 cm) and documented in `validation.md`.
- 72-hour stability test is documented as a known open item planned for Sprint 2 (consistent with tag).

**Dependencies**
T8 (full firmware working for all 3 anchors).

---

## Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| BLE passive scan RSSI is degraded by concurrent WiFi traffic (coexistence jitter) | Medium | Medium | Validate in T0 spike. If RSSI std dev is unacceptably high under WiFi load, increase `ANCHOR_EWA_ALPHA` for heavier smoothing. Document worst-case degradation in `validation.md`. |
| NimBLE passive scan drops packets during high WiFi throughput (coex contention) | Medium | Medium | Reduce WiFi keepalive frequency in sdkconfig if needed. RSSI is sampled at 200 ms intervals — occasional missed packets are tolerable and will smooth out via EWA. |
| MQTT reconnect state machine deadlocks (event handler + `wifi_mqtt_ensure_connected()` race) | Low | High | Use FreeRTOS event groups in `wifi_mqtt.c` to signal connection state. Never call `esp_mqtt_client_publish()` from within the MQTT event handler — only from the main task. |
| OTA rollback not working — `esp_ota_mark_app_valid_cancel_rollback()` not called at boot | Medium | High | Call placed at step 2 of every boot (before any blocking init), identical to the tag pattern. Rollback persistence test is a required acceptance criterion for T7. |
| All 3 anchors use the same MQTT client ID — broker disconnects one when another connects | High | High | **Resolved in T1**: each anchor has a unique `ANCHOR_MQTT_CLIENT_ID` (`anchor_A1`, `anchor_A2`, `anchor_A3`) in its respective `anchor_config_*.h`. |
| WS2812 LED on GPIO 48 requires RMT — plain GPIO toggle does not work on DevKitC-1 | High | Low | Same issue as tag, already solved with `espressif/led_strip` component. Mitigated in T1 (`idf_component.yml`) and T6 implementation. |
| SNTP sync fails in lab environment (NTP blocked by firewall or no internet) | Low | Medium | Use a local NTP server (e.g., router built-in NTP) and set `ANCHOR_NTP_SERVER` to its IP. Document fallback in README. |
| `anchor_config.h` stub omits a required constant — silent compile error instead of meaningful message | Medium | Low | T1 requires `static_assert` on `ANCHOR_EWA_ALPHA`. For other constants, the linker will emit an "undefined reference" error on first use. Document all constants in README as required fields. |

---

## Out of Scope (MVP)

Mirrors the spec's "Out of Scope" section; listed here to prevent scope creep during implementation:

- Active BLE scanning (no scan requests)
- Scanning for more than one tag simultaneously
- RSSI-based filtering of non-Eddystone packets beyond UUID matching
- TLS for the local OTA HTTP server
- Cloud MQTT broker support
- Sub-millisecond timestamp precision
- NVS persistence of reset count or uptime (tag has this; anchor does not)
- RSSI deduplication or change-threshold publish logic
- Multi-anchor MQTT fan-out or broker-side processing
- 72-hour stability test (Sprint 2)
- WiFi provisioning (credentials are compile-time constants in MVP)
