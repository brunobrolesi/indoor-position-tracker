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
| A7 | The project uses ESP-IDF v6.0's `esp_bt` / NimBLE or Bluedroid stack for Extended Advertising | ESP-IDF v6.0 supports Extended Advertising via `esp_gap_ble_api`; confirm API surface before coding T3 |
| Q1 | Which BLE stack is preferred: Bluedroid or NimBLE? Extended Advertising API differs between them | **Must answer before T3** |
| Q2 | Should the NVS namespace be `tag_nvs` or follow a project-wide convention? | Decide before T6 |
| Q3 | Is there a project-wide `sdkconfig` baseline, or is each component responsible for its own? | Decide before T1 |

---

## Task Breakdown

Tasks are ordered by dependency.

---

### T1 — Project Scaffold & Build System

**Description**
Create the ESP-IDF project skeleton under `tag/firmware/`. This includes `CMakeLists.txt`, `sdkconfig.defaults`, the `main/` component, and the header file `tag_config.h` that centralises all compile-time constants.

**Acceptance Criteria**
- `idf.py build` succeeds with an empty `app_main()`.
- `tag_config.h` defines every constant listed below with its placeholder/default value.
- `sdkconfig.defaults` enables Bluetooth (BLE), disables Classic BT, enables NVS, enables OTA partitions, and sets partition table to `partitions_ota.csv`.

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

**Inputs / Outputs**
- Input: `tag_config.h` constants.
- Output: `main/ble_adv.c`, `main/ble_adv.h`; public API: `ble_adv_init()`, `ble_adv_start()`, `ble_adv_stop()`.

**Dependencies**
T1, T2 (project + partitions must exist).

**Testing Notes**
- Verify with nRF Connect that service UUID `0xFEAA` is present.
- Capture with nRF Sniffer or Wireshark BLE to confirm Coded PHY.
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
- The advertising task feeds the watchdog at least once per second.
- A simulated hang (infinite loop without feeding) triggers a reset within 5 seconds (test in dev; remove before production flash).

**Inputs / Outputs**
- Output: watchdog initialisation in `main/app_main.c` using `esp_task_wdt_init()` and `esp_task_wdt_add()`.

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
 └─ GPIO LOW? ──Yes──> OTA Mode
 |                       ├─ Init WiFi (STA mode)
 |                       ├─ Connect (30s timeout) ──fail──> reboot
 |                       ├─ HTTP GET firmware (60s timeout) ──fail──> reboot
 |                       ├─ esp_ota_write() stream
 |                       ├─ esp_ota_end() + esp_ota_set_boot_partition()
 |                       └─ esp_restart() → new firmware
 └─ GPIO HIGH? ──No──> Normal mode (BLE advertising)
```

**Acceptance Criteria**
- Holding `OTA_TRIGGER_GPIO` LOW during boot enters OTA mode.
- BLE advertising is suspended during OTA (`ble_adv_stop()` called before WiFi init).
- WiFi connects within 30 seconds; if not, device reboots with previous firmware.
- Firmware download completes within 60 seconds; if not, device reboots with previous firmware.
- After successful OTA: device reboots, new firmware runs, BLE advertising resumes.
- Rollback test: interrupt HTTP server during download → device reboots with original firmware and BLE advertising resumes.

**Inputs / Outputs**
- Output: `main/ota_update.c`, `main/ota_update.h`; public API: `ota_check_and_run()` (returns if GPIO not triggered).
- Requires WiFi STA mode (`esp_wifi`) and `esp_https_ota` / `esp_http_client` + `esp_ota_ops`.

**Dependencies**
T1, T2 (OTA partitions), T3 (BLE adv stop/start API).

**Testing Notes**
- Use `python3 -m http.server 8070` in `tag/firmware/build/` directory to serve `tag.bin`.
- Test happy path: complete OTA and verify new firmware version reported in logs.
- Test rollback: kill HTTP server mid-download; verify previous firmware resumes advertising.
- Test WiFi failure: wrong SSID in constant → verify reboot within 30 s.

---

### T8 — Integration: `app_main()` Wiring

**Description**
Wire all modules together in `app_main()` in the correct boot sequence order.

**Boot sequence**

```
1. nvs_flash_init()              // Required for BT and NVS modules
2. nvs_log_init()                // Read/increment reset counter
3. led_status_init()             // Configure GPIO
4. led_status_start()            // Start 1 Hz blink
5. ota_check_and_run()           // Check GPIO; may not return if OTA triggered
6. ble_adv_init()                // Init BT stack, configure Extended Adv
7. ble_adv_start()               // Start advertising
8. wdt_init()                    // Register main task with TWDT
9. nvs_log_start_uptime_timer()  // Start 60s NVS persist timer
10. main loop: feed watchdog every 1s
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
1. `tag/specs/001-mvp/validacao.md` — records:
   - Average current draw (mA) at 100 ms advertising interval
   - Packet loss rate (%) measured at 10 m LOS
   - Boot-to-first-adv latency (ms)
2. `tag/firmware/README.md` — includes:
   - Prerequisites (ESP-IDF v6.0, toolchain)
   - How to set constants in `tag_config.h`
   - `idf.py build && idf.py flash` command
   - How to set up OTA HTTP server
   - How to verify with nRF Connect
3. Git tag `tag-firmware-v1.0` on the delivery commit.

**Acceptance Criteria**
- All DoD checkboxes in `spec.md` are checked.
- `validacao.md` exists with measured values.
- `README.md` is complete and a new developer can build + flash following it alone.

**Dependencies**
T8 (full firmware working).

---

## Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| ESP-IDF v6.0 Extended Advertising API differs significantly between Bluedroid and NimBLE | High | High | Answer Q1 immediately; prototype T3 first in an isolated test project before integrating |
| `BLE_TX_POWER_1M_DBM` placeholder (-65 dBm) is inaccurate, degrading distance estimates in anchors | Medium | Medium | Flag in `tag_config.h` with a prominent `TODO: measure at 1m` comment; schedule bench measurement before field testing |
| OTA rollback not working correctly (esp_ota_set_boot_partition not called before esp_restart) | Medium | High | Write explicit rollback test in T7; do not mark T7 done until rollback is confirmed |
| WiFi credentials hardcoded in firmware (security) | Low (lab) | Low (MVP) | Document in README as MVP-only; add TODO for provisioning in a future sprint |
| Coded PHY S=8 not supported or incorrectly configured on the chosen ESP32 module variant | Low | High | Verify with nRF Sniffer in T3 before proceeding to integration; confirm ESP32-WROOM-32 supports BLE 5.0 Extended Advertising on the specific silicon revision in use |

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
