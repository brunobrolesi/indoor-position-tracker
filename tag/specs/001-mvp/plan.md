# Implementation Plan — EPIC-01: Tag Firmware (MVP)

## Overview

This plan describes how to implement the ESP32 BLE tag firmware for the indoor positioning MVP. The firmware is written in C using ESP-IDF v6.0 and must continuously broadcast Eddystone-UID packets over BLE Extended Advertising (Coded PHY S=8) every 100 ms. Supporting features include a 1 Hz LED heartbeat, a software watchdog, OTA update via HTTP over WiFi, and persistent uptime/reset logging in NVS. The implementation is organized as a single ESP-IDF component project with compile-time constants controlling all RF and GPIO parameters.

---

## Assumptions & Open Questions

| # | Assumption / Question | Status |
|---|----------------------|--------|
| A1 | `LED_STATUS_GPIO = 2` (built-in LED on ESP32-WROOM-32 DevKit) | Placeholder — confirm from schematic |
| A2 | `OTA_TRIGGER_GPIO = 0` (BOOT button) | Placeholder — confirm from schematic |
| A3 | `BLE_TX_POWER_1M_DBM = -65` (RSSI at 1 m) | Placeholder — measure in bench test |
| A4 | `BLE_TX_POWER_DBM = 0` (radio TX power) | Default value; valid range -12 to +9 dBm |
| A5 | `BLE_ADV_INTERVAL_MS = 100` | Default; valid range 50–500 ms |
| A6 | OTA_SERVER_URL points to a Python HTTP server on the local lab network | No production server required for MVP |
| A7 | The project uses ESP-IDF v6.0's NimBLE stack for Extended Advertising | NimBLE chosen over Bluedroid: cleaner Extended Advertising API in IDF v6.0, lower memory footprint, actively maintained. See T0. |
| Q1 | Which BLE stack is preferred: Bluedroid or NimBLE? Extended Advertising API differs between them | **Resolved — NimBLE. See T0.** |
| Q2 | Should the NVS namespace be `tag_nvs` or follow a project-wide convention? | **Resolved — `tag_nvs` is the chosen namespace.** |
| Q3 | Is there a project-wide `sdkconfig` baseline, or is each component responsible for its own? | **Resolved — each component owns its own `sdkconfig.defaults`. The tag firmware is a self-contained ESP-IDF project under `tag/firmware/`; no shared baseline exists yet. T1 is responsible for the full `sdkconfig.defaults` for this project.** |

---

## Task Breakdown

Tasks are ordered by dependency.

---

### T0 — BLE Stack Spike (NimBLE Extended Advertising)

**Description**
Validate that ESP-IDF v6.0's NimBLE stack supports Extended Advertising with Coded PHY S=8 on the target hardware (ESP32-WROOM-32) before any integration work begins. This task resolves Q1 and de-risks T3.

**Rationale for NimBLE over Bluedroid**
NimBLE provides a cleaner Extended Advertising API in IDF v6.0 (`ble_gap_ext_adv_*`), has a lower memory footprint (~20 KB less heap), and is the actively maintained stack for new features in ESP-IDF. Bluedroid's Extended Advertising support is available but its API is more complex and less documented.

**Acceptance Criteria**
- `sdkconfig` sets `CONFIG_BT_NIMBLE_ENABLED=y` and `CONFIG_BT_BLUEDROID_ENABLED=n`.
- A minimal sketch (separate from the main project) starts an Extended Advertising set with Coded PHY S=8 and transmits at least one packet visible in nRF Connect.
- Coded PHY S=8 confirmed via nRF Sniffer or nRF Connect PHY display.
- No memory allocation failures in `idf.py monitor` during BLE init.

**Inputs / Outputs**
- Output: confirmed NimBLE API path documented in a code comment block at the top of `main/ble_adv.c`; spike branch can be discarded after T3 is merged.
- Output: the following must be documented as T0 deliverables and carried into T1/T3:
  1. **Per-packet counter update mechanism** — confirm whether `BLE_GAP_EVENT_ADV_COMPLETE` fires for non-connectable non-scannable Extended Advertising sets on ESP32 (it may not). If not, document that a FreeRTOS task + `esp_timer` at `BLE_ADV_INTERVAL_MS` is required to call `ble_gap_ext_adv_set_adv_data()` before each event.
  2. **`on_sync` callback pattern** — confirm the NimBLE host async init sequence: `nimble_port_init()` → register `ble_hs_cfg.sync_cb` → `nimble_port_freertos_init()` → wait for `sync_cb` before any `ble_gap_*` call.
  3. **All required `sdkconfig` flags** for Extended Advertising — at minimum: `CONFIG_BT_NIMBLE_EXT_ADV=y`, `CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT` (tune upward if adv data > 31 bytes), `CONFIG_BT_NIMBLE_HS_TASK_STACK_SIZE` (minimum 4096 recommended). Record exact values that pass the smoke test.
  4. **Heap usage** during BLE init — record free heap before and after `nimble_port_init()` to pre-empt `CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT` exhaustion under Extended Advertising payloads.

**Dependencies**
None — can run in parallel with T1.

**Testing Notes**
- If NimBLE Extended Advertising fails on the silicon revision in use, escalate immediately — this invalidates the entire RF implementation path and requires re-evaluating Q1.

---

### T1 — Project Scaffold & Build System

**Description**
Create the ESP-IDF project skeleton under `tag/firmware/`. This includes `CMakeLists.txt`, `sdkconfig.defaults`, the `main/` component, and the header file `tag_config.h` that centralises all compile-time constants.

**Acceptance Criteria**
- `idf.py build` succeeds with an empty `app_main()`.
- `tag_config.h` defines every constant listed below with its placeholder/default value.
- `sdkconfig.defaults` enables Bluetooth (BLE), disables Classic BT, enables NVS, enables OTA partitions, and sets partition table to `partitions_ota.csv`.
- `sdkconfig.defaults` includes the following NimBLE-specific settings (exact values confirmed in T0):
  ```
  CONFIG_BT_NIMBLE_ENABLED=y
  CONFIG_BT_BLUEDROID_ENABLED=n
  CONFIG_BT_NIMBLE_EXT_ADV=y
  CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT=12
  CONFIG_BT_NIMBLE_HS_TASK_STACK_SIZE=4096
  CONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES=1
  CONFIG_BT_NIMBLE_MAX_EXT_ADV_DATA_LEN=31
  ```
  > Note: `CONFIG_BT_NIMBLE_EXT_ADV=y` is NOT enabled by default in ESP-IDF v6.0 — omitting it causes a compile-time error when calling `ble_gap_ext_adv_configure()`. Values above are starting points; adjust based on T0 heap measurements.

**Constants to define in `tag_config.h`**

```c
#define BLE_ADV_INTERVAL_MS       100
#define BLE_TX_POWER_DBM          0
#define BLE_TX_POWER_1M_DBM       (-65)
#define LED_STATUS_GPIO           2
#define OTA_TRIGGER_GPIO          0
#define OTA_WIFI_SSID             "CHANGE_ME"
#define OTA_WIFI_PASSWORD         "CHANGE_ME"
#define OTA_SERVER_URL            "http://192.168.1.100:8070/tag-firmware.bin"
#define OTA_CONN_TIMEOUT_SEC      30
#define OTA_DOWNLOAD_TIMEOUT_SEC  60
#define WDT_TIMEOUT_SEC           5
#define NVS_UPTIME_PERSIST_SEC    60
#define TAG_UUID  "019d8ba3-5d5f-792b-8e60-906fbeca324a"
```

**Inputs / Outputs**
- Output: `tag/firmware/` directory tree, compilable project.

**Dependencies**
None — first task.

**Testing Notes**
- Verify `idf.py build` with ESP-IDF v6.0 toolchain.
- Verify that changing a constant in `tag_config.h` and rebuilding propagates correctly (grep for usage).

---

### T2 — Partition Table (Safe OTA)

**Description**
Create a custom partition table `partitions_ota.csv` that supports Safe OTA: two `app` partitions (`ota_0`, `ota_1`) and one `otadata` partition, alongside `nvs` and `phy_init` data partitions.

**Acceptance Criteria**
- Partition table compiles without warnings.
- `idf.py partition-table` outputs the expected layout.
- Both `ota_0` and `ota_1` are large enough to hold the firmware image (≥ 1.5 MB each recommended for ESP32 with 4 MB flash).

**Suggested layout (4 MB flash)**

```
# Name,    Type, SubType,  Offset,   Size
nvs,       data, nvs,      0x9000,   0x6000
otadata,   data, ota,      0xf000,   0x2000
phy_init,  data, phy,      0x11000,  0x1000
ota_0,     app,  ota_0,    0x20000,  0x180000
ota_1,     app,  ota_1,    0x1A0000, 0x180000
```

**Inputs / Outputs**
- Output: `tag/firmware/partitions_ota.csv`, referenced in `CMakeLists.txt` and `sdkconfig.defaults`.

**Dependencies**
T1 (project scaffold must exist).

**Testing Notes**
- Flash to DevKit and verify with `idf.py partition-table-flash` + `esptool.py read_flash_status`.
- Confirm `esp_ota_ops` API can retrieve active partition without error.

---

### T3 — BLE Extended Advertising (Core — RF-01 to RF-04)

**Description**
Implement the BLE advertising module (`ble_adv.c` / `ble_adv.h`). This is the most critical module. It initialises the BLE stack, configures Extended Advertising with Coded PHY S=8 on channels 37/38/39, builds the Eddystone-UID payload, and starts continuous non-connectable non-scannable advertising at the configured interval.

**Eddystone-UID Payload Layout (17 bytes of service data)**

```
Offset  Len  Field
0       1    Frame Type = 0x00 (UID)
1       1    Ranging Data = BLE_TX_POWER_1M_DBM (signed int8)
2       10   Namespace = first 10 bytes of UUID
12      6    Instance = last 6 bytes of UUID
16      2    Reserved → sequence counter (uint16_t LE, rollover 0xFFFF→0x0000)
```

UUID `019d8ba3-5d5f-792b-8e60-906fbeca324a` maps to:
- Namespace: `01 9D 8B A3 5D 5F 79 2B 8E 60`
- Instance:   `90 6F BE CA 32 4A`

The sequence counter starts at 0 on boot and increments on every advertising event.

**Full AD structure** (inside the advertising data buffer):
1. Flags AD type (`0x01`): `0x06` (LE General Discoverable + BR/EDR not supported)
2. Complete List of 16-bit UUIDs (`0x03`): `0xAA 0xFE` (Eddystone service UUID, little-endian)
3. Service Data (`0x16`): `0xAA 0xFE` + 17-byte payload above

**Acceptance Criteria**
- nRF Connect (or equivalent scanner) shows the tag advertising with Coded PHY S=8.
- Eddystone-UID frame is decoded correctly: Namespace and Instance match the UUID mapping above.
- Sequence counter increments by 1 per packet with correct rollover (0xFFFF → 0x0000).
- Advertising interval is 100 ms ± 10 ms (confirmed with nRF Sniffer or logic analyser).
- Advertising type is non-connectable, non-scannable (`ADV_TYPE_NONCONN_IND`).

**NimBLE API path (resolved in T0)**
Use `ble_gap_ext_adv_configure()` + `ble_gap_ext_adv_set_adv_data()` + `ble_gap_ext_adv_start()`. Set `OWN_ADDR_TYPE` to public, PHY to `BLE_HCI_LE_PHY_CODED`, and `secondary_phy` to `BLE_HCI_LE_PHY_CODED` with S=8 coding (`BLE_HCI_LE_PHY_CODED_S8_PREF`).

**NimBLE async initialisation (`on_sync` requirement)**
NimBLE's host initialises asynchronously. No `ble_gap_*` API may be called before the host signals readiness. The required init sequence is:
1. `nimble_port_init()` — initialises the host and controller.
2. Assign `ble_hs_cfg.sync_cb = ble_adv_on_sync` before calling `nimble_port_freertos_init()`.
3. `nimble_port_freertos_init(nimble_host_task)` — starts the NimBLE host FreeRTOS task.
4. `ble_adv_on_sync()` is called by the NimBLE host task once the host-controller link is established. All `ble_gap_ext_adv_configure()` / `ble_gap_ext_adv_set_adv_data()` / `ble_gap_ext_adv_start()` calls must happen inside or after this callback.
5. `ble_adv_init()` (public API) performs steps 1–3. `ble_adv_start()` must be called from `on_sync` context or after `on_sync` has fired (use a FreeRTOS event group or semaphore if calling from `app_main`).

**Sequence counter update mechanism**
`BLE_GAP_EVENT_ADV_COMPLETE` does **not** fire for non-connectable, non-scannable Extended Advertising sets in ESP-IDF v6.0 NimBLE (confirmed in T0). Therefore the sequence counter must be updated via a periodic software timer:
- Create an `esp_timer` with period `BLE_ADV_INTERVAL_MS` ms started inside `ble_adv_start()`.
- On each timer callback: increment `seq`, rebuild the advertising data buffer with the new counter value, call `ble_gap_ext_adv_set_adv_data()` with the updated buffer, then call `ble_gap_ext_adv_start()` with `duration=1` and `max_events=1` so the controller sends exactly one PDU per timer tick.
- This approach tightly couples the software timer period to the advertising interval; jitter between the timer and the controller scheduler is acceptable (< ±5 ms typical on ESP32 FreeRTOS with `portTICK_PERIOD_MS = 1`).

> **Alternative (if T0 confirms `ADV_COMPLETE` fires):** register a `ble_gap_event_fn` callback for `BLE_GAP_EVENT_ADV_COMPLETE`; increment and resubmit adv data there. Document the confirmed path in `ble_adv.c` header comment.

**Sequence counter concurrency**
The sequence counter (`uint16_t seq`) must be declared as `static volatile uint16_t`. Since the `esp_timer` callback runs in the ESP timer task context (not `app_main`), protect the increment with `portENTER_CRITICAL_ISR` / `portEXIT_CRITICAL_ISR` or `__atomic_fetch_add(&seq, 1, __ATOMIC_RELAXED)` if the counter is only written from the timer callback (single writer).

**Inputs / Outputs**
- Input: `tag_config.h` constants.
- Output: `main/ble_adv.c`, `main/ble_adv.h`; public API: `ble_adv_init()`, `ble_adv_start()`, `ble_adv_stop()`.

**Dependencies**
T0 (NimBLE spike validated), T1, T2 (project + partitions must exist).

**Testing Notes**
- Verify with nRF Connect that service UUID `0xFEAA` is present.
- Capture with nRF Sniffer or Wireshark BLE to confirm Coded PHY S=8 (not S=2).
- Test sequence counter rollover by letting the tag run for ~6553 seconds (or mock counter at 0xFFFE in a unit test).
- Test `ble_adv_stop()` / `ble_adv_start()` cycle (needed for OTA module).

---

### T4 — LED Heartbeat (RF-05)

**Description**
Implement a 1 Hz LED blink using an ESP-IDF software timer (`esp_timer`). The LED must toggle independently of the advertising interval.

**Acceptance Criteria**
- LED blinks at 1 Hz (500 ms ON / 500 ms OFF) confirmed with oscilloscope or visual inspection.
- Blink continues even if advertising is temporarily suspended (OTA mode).
- GPIO pin is controlled by `LED_STATUS_GPIO` constant.

**Inputs / Outputs**
- Output: `main/led_status.c`, `main/led_status.h`; public API: `led_status_init()`, `led_status_start()`.

**Dependencies**
T1.

**Testing Notes**
- Measure LED toggle frequency with oscilloscope or logic analyser.
- Verify that LED is driven as output (not input) and that GPIO is configured with `GPIO_MODE_OUTPUT`.

---

### T5 — Watchdog (RF-06)

**Description**
Configure the ESP-IDF Task Watchdog Timer (TWDT) with a 5-second timeout. The main advertising task must feed the watchdog regularly (every ~1 second). If the firmware hangs, the TWDT triggers an automatic reset.

**Acceptance Criteria**
- Watchdog is initialised with `WDT_TIMEOUT_SEC = 5` seconds.
- The **main task** (the FreeRTOS task running `app_main`) is registered with TWDT via `esp_task_wdt_add(NULL)` — `NULL` registers the calling task. There is no separate "advertising task"; NimBLE runs its own internal host task which is not registered.
- The main loop in `app_main` calls `esp_task_wdt_reset()` every ~1 second (via `vTaskDelay(pdMS_TO_TICKS(1000))`).
- A simulated hang (infinite loop without feeding) triggers a reset within 5 seconds (test in dev; remove before production flash).

**Inputs / Outputs**
- Output: watchdog initialisation in `main/app_main.c` using `esp_task_wdt_init()` and `esp_task_wdt_add(NULL)`.

**Dependencies**
T1, T3 (advertising task must exist to be registered with TWDT).

**Testing Notes**
- Temporarily add an infinite loop after 10 seconds of operation and verify reset occurs within 5 s.
- Verify that the reset reason is `ESP_RST_TASK_WDT` via `esp_reset_reason()`.

---

### T6 — NVS Persistence: Uptime & Reset Counter (RF-08)

**Description**
Implement persistent logging of uptime and reset count in ESP32 NVS. On every boot, read and increment the reset counter. During normal operation, persist the cumulative uptime to NVS every 60 seconds using a software timer.

**NVS keys** (namespace `tag_nvs`):
- `reset_count` — `uint32_t`
- `uptime_sec` — `uint32_t` (cumulative seconds across reboots)

**Acceptance Criteria**
- After a power cycle, `reset_count` is higher than before.
- After running for 65+ seconds, `uptime_sec` in NVS reflects the accumulated time.
- NVS values survive a firmware reset (TWDT-triggered or manual).
- NVS values survive an OTA update (NVS partition is not erased during OTA).

**Inputs / Outputs**
- Output: `main/nvs_log.c`, `main/nvs_log.h`; public API: `nvs_log_init()`, `nvs_log_increment_reset_count()`, `nvs_log_start_uptime_timer()`.

**Dependencies**
T1 (project scaffold, partition table has NVS partition).

**Testing Notes**
- Power-cycle the device 5 times and verify `reset_count` = 5.
- Run for 3 minutes, read NVS with `idf.py monitor` + custom print command, verify `uptime_sec` ≈ 180.
- Trigger a TWDT reset and verify both counters persist.

---

### T7 — OTA Update via HTTP (RF-07a to RF-07e)

**Description**
Implement the OTA module. On boot, check `OTA_TRIGGER_GPIO` level. If LOW (button held / jumper installed), enter OTA mode: suspend BLE advertising, connect to WiFi, download firmware from `OTA_SERVER_URL`, validate, and reboot. On success, the new firmware runs. On failure (timeout, HTTP error, corrupt image), reboot with the previous firmware (Safe OTA / rollback).

**State machine**

```
Boot
 └─ [Step A — always, every boot] Call esp_ota_mark_app_valid_cancel_rollback()
 |     This is a no-op if no OTA update is pending validation (cold boot, normal reset).
 |     If this boot follows a successful OTA, it confirms the new firmware — without it
 |     the bootloader rolls back on the very next reboot. Must run before any blocking init.
 └─ Configure OTA_TRIGGER_GPIO pull-up (gpio_set_pull_mode PULLUP_ONLY)
 └─ GPIO LOW? ──Yes──> OTA Mode
 |                       ├─ ble_adv_stop() — suspend advertising before WiFi init
 |                       ├─ [WiFi/BLE co-existence] NimBLE host and BT controller remain
 |                       |   initialised; ESP32 coex arbiter handles radio sharing.
 |                       |   Do NOT call nimble_port_deinit() — it is unnecessary and slow.
 |                       |   esp_coex is enabled by default when both BT and WiFi are active.
 |                       ├─ Init WiFi (STA mode)
 |                       ├─ Connect (30s timeout) ──fail──> esp_wifi_disconnect() + esp_wifi_stop() → reboot
 |                       ├─ HTTP GET firmware (60s timeout) ──fail──> esp_wifi_disconnect() + esp_wifi_stop() → reboot
 |                       ├─ esp_ota_write() stream
 |                       ├─ esp_ota_end() + esp_ota_set_boot_partition()
 |                       ├─ esp_wifi_disconnect() + esp_wifi_stop() + esp_wifi_deinit()
 |                       └─ esp_restart() → new firmware boots
 |                              └─ Step A above fires first → marks firmware valid → no rollback
 └─ GPIO HIGH? ──No──> Normal mode (BLE advertising)
```

**Acceptance Criteria**
- `OTA_TRIGGER_GPIO` is configured with internal pull-up (`GPIO_PULLUP_ONLY`) before sampling — prevents spurious OTA trigger on custom hardware without external pull-up.
- Holding `OTA_TRIGGER_GPIO` LOW during boot enters OTA mode.
- BLE advertising is suspended during OTA (`ble_adv_stop()` called before WiFi init).
- WiFi connects within 30 seconds; if not, device reboots with previous firmware.
- Firmware download completes within 60 seconds; if not, device reboots with previous firmware.
- `esp_ota_mark_app_valid_cancel_rollback()` is called at the very start of every boot (Step A in the state machine above), before any blocking init. On normal/cold boots this is a no-op. On the first boot after OTA it commits the new firmware; without it the bootloader silently rolls back on the next reboot.
- After successful OTA: device reboots → Step A confirms new firmware → BLE advertising resumes normally.
- Rollback test: interrupt HTTP server during download → device reboots → Step A is a no-op (OTA was not committed) → original firmware resumes advertising.
- Rollback persistence test: after successful OTA, power-cycle the device a **second** time and verify it stays on the new firmware (confirms Step A fired correctly on the post-OTA boot).
- WiFi is cleanly stopped (`esp_wifi_disconnect()` + `esp_wifi_stop()` + `esp_wifi_deinit()`) on both success and failure paths before rebooting.

**Inputs / Outputs**
- Output: `main/ota_update.c`, `main/ota_update.h`; public API: `ota_check_and_run()` (returns if GPIO not triggered).
- Requires WiFi STA mode (`esp_wifi`) and `esp_https_ota` / `esp_http_client` + `esp_ota_ops`.

**Dependencies**
T1, T2 (OTA partitions), T3 (BLE adv stop/start API).

**Testing Notes**
- Use `python3 -m http.server 8070` in `tag/firmware/build/` directory to serve `tag.bin`.
- Test happy path: complete OTA → verify new firmware version reported in logs → power-cycle → verify device stays on new firmware (rollback persistence test).
- Test rollback: kill HTTP server mid-download → verify previous firmware resumes advertising → confirm `idf.py monitor` shows `ESP_OTA_IMG_INVALID` was never written (i.e. rollback occurred correctly).
- Test WiFi failure: wrong SSID in constant → verify reboot within 30 s.
- Test WiFi cleanup: after OTA failure, verify `idf.py monitor` shows WiFi deinitialized before reboot — no leaked tasks or heap.

---

### T8 — Integration: `app_main()` Wiring

**Description**
Wire all modules together in `app_main()` in the correct boot sequence order.

**Boot sequence**

```
1.  nvs_flash_init()                         // Required for BT and NVS modules
2.  esp_ota_mark_app_valid_cancel_rollback()  // Confirm this firmware is healthy (no-op on
                                              // cold boot; commits firmware on post-OTA boot).
                                              // Implemented here AND inside ota_check_and_run()
                                              // (Step A of T7 state machine). Both paths are
                                              // identical — this line ensures it runs even if
                                              // ota_check_and_run() is not reached.
3.  esp_task_wdt_init(WDT_TIMEOUT_SEC, true) // Init TWDT early — protects all subsequent inits
    esp_task_wdt_add(NULL)                   // Register the main task (NULL = calling task)
4.  nvs_log_init()                           // Read/increment reset counter
5.  led_status_init()                        // Configure GPIO
6.  led_status_start()                       // Start 1 Hz blink
7.  ota_check_and_run()                      // Check GPIO (with pull-up); may not return if OTA triggered
8.  ble_adv_init()                           // nimble_port_init() + register on_sync cb + nimble_port_freertos_init()
                                              // Returns before on_sync fires — ble_adv_start() is
                                              // called from on_sync callback internally.
9.  // ble_adv_start() is called internally from the on_sync callback registered in ble_adv_init()
10. nvs_log_start_uptime_timer()             // Start 60s NVS persist timer
11. main loop: esp_task_wdt_reset() + vTaskDelay(pdMS_TO_TICKS(1000))
```

**Acceptance Criteria**
- Full boot to first advertising packet < 2 seconds (measured with logic analyser on LED GPIO and nRF Sniffer).
- All modules initialise without error logs.
- No task stack overflow warnings in `idf.py monitor`.

**Dependencies**
T3, T4, T5, T6, T7 (all modules complete).

**Testing Notes**
- Run `idf.py monitor` during boot and verify each init step logs `[OK]` or equivalent.
- Use nRF Sniffer to measure time from power-on to first advertising packet.

---

### T9 — Validation & Documentation

**Description**
Run the Definition of Done checklist from the spec, record measurements, and publish documentation.

**Deliverables**
1. `tag/specs/001-mvp/validacao.md` — records the following with the specific methodology used:
   - Average current draw (mA): measure with a series shunt resistor (1 Ω) + multimeter in DC current mode, or a USB power meter, at steady-state advertising (≥ 30 s running). Record peak and average.
   - Packet loss rate (%): capture with nRF Sniffer (Wireshark) for exactly 1000 expected advertising events at 10 m LOS in an indoor corridor (no obstructions). Loss = (1000 − received) / 1000 × 100. Threshold: < 2%.
   - Boot-to-first-adv latency (ms): measure from power-on (logic analyser trigger on VCC) to first Coded PHY packet captured by nRF Sniffer. Record 5 samples, report min/max/avg.
2. `tag/firmware/README.md` — includes:
   - Prerequisites (ESP-IDF v6.0, toolchain)
   - How to set constants in `tag_config.h`
   - `idf.py build && idf.py flash` command
   - How to set up OTA HTTP server
   - How to verify with nRF Connect
3. Hardware compatibility: repeat BLE advertising smoke test on **ESP32-WROVER** (has PSRAM — may shift heap layout and affect NimBLE stack allocation). Record result in `validacao.md`.
4. Git tag `tag-firmware-v1.0` on the delivery commit.

**Acceptance Criteria**
- All DoD checkboxes in `spec.md` are checked.
- `validacao.md` exists with measured values and measurement methodology.
- Packet loss < 2% confirmed with methodology above on both WROOM-32 and WROVER.
- Current draw < 20 mA confirmed and recorded. `validacao.md` must include the battery life calculation: `500 mAh / I_avg_mA = estimated_hours` — confirm ≥ 8 h as required by the spec non-functional guarantee.
- Source code comments are written in English throughout all `.c` / `.h` files (spec DoD requirement).
- `README.md` is complete and a new developer can build + flash following it alone.

**Note on 24h stability test**
The spec lists "24h continuous operation without spontaneous reboot" as a non-functional guarantee. This is explicitly deferred to Sprint 2 per the spec. Do not block `tag-firmware-v1.0` delivery on it, but document it as a known open item in `validacao.md`.

**Dependencies**
T8 (full firmware working).

---

## Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| ESP-IDF v6.0 Extended Advertising API differs significantly between Bluedroid and NimBLE | High | High | **Resolved — NimBLE chosen (T0).** Prototype in T0 spike before integrating in T3. |
| `BLE_TX_POWER_1M_DBM` placeholder (-65 dBm) is inaccurate, degrading distance estimates in anchors | Medium | Medium | Flag in `tag_config.h` with a prominent `TODO: measure at 1m` comment; schedule bench measurement before field testing |
| OTA rollback not working correctly — missing `esp_ota_mark_app_valid_cancel_rollback()` causes silent rollback on every reboot after OTA | Medium | High | Call placed at top of every boot (T7 state machine Step A + T8 boot sequence step 2) — runs as a no-op on cold boots, commits firmware on post-OTA boots. T7 acceptance criteria require rollback persistence test (two reboots after OTA). Do not mark T7 done until persistence test passes. |
| WiFi credentials hardcoded in firmware (security) | Low (lab) | Low (MVP) | Document in README as MVP-only; add TODO for provisioning in a future sprint |
| Coded PHY S=8 not supported or incorrectly configured on the chosen ESP32 module variant | Low | High | Verified in T0 spike before T3 starts; confirm ESP32-WROOM-32 silicon revision supports BLE 5.0 Extended Advertising. Also test on WROVER in T9. |
| OTA GPIO floats on custom PCB without external pull-up, triggering OTA on every boot | Low | High | T7 requires `gpio_set_pull_mode(OTA_TRIGGER_GPIO, GPIO_PULLUP_ONLY)` before sampling. |
| Sequence counter data race between BLE stack task and advertising callback | Low | Medium | T3 uses a single-writer `esp_timer` callback for counter updates; atomic fetch-add or `portENTER_CRITICAL_ISR` guard is sufficient. No BLE stack task writes the counter directly. |
| `CONFIG_BT_NIMBLE_EXT_ADV=y` missing from sdkconfig causes compile-time failure | High | High | **Resolved** — added to T1 `sdkconfig.defaults` required list. T0 confirms exact value. |
| NimBLE `on_sync` async init ignored, causing runtime `BLE_HS_ENOTSTARTED` errors | High | High | **Resolved** — T3 now specifies the full `nimble_port_init()` → `sync_cb` → `nimble_port_freertos_init()` sequence. T8 boot sequence updated accordingly. |

---

## Out of Scope (MVP)

- BLE payload encryption or rotating UUID/MAC
- Secure OTA (HTTPS/TLS)
- WiFi provisioning (SSID/password are compile-time constants)
- Battery management (BMS, low-battery alerts)
- Runtime configuration of advertising interval or TX power
- Multi-tag support (single UUID only)
- 24-hour stability test (planned for Sprint 2)
- Factory flash / NVS provisioning of UUID per device
