## EPIC-02 · Anchor Module (Fixed Reference Node)

| Field | Value |
|-------|-------|
| **Module** | Hardware + Firmware |
| **Tech stack** | ESP32-S3 · ESP-IDF v6.0 · BLE · MQTT · WiFi |
| **Owner** | Hardware Team |
| **Target sprint** | Sprint 1–2 |
| **Status** | To Do |

### Description

Anchors are fixed nodes installed at known positions on the environment floor plan. Each anchor continuously scans the BLE signal from the tag, measures RSSI, applies an on-chip EWA (Exponential Weighted Average) pre-filter to reduce noise, and publishes readings to the MQTT broker. They are the interface between the physical radio world and the digital processing pipeline.

### Context and motivation

The anchor positions are critical for the geometric accuracy of trilateration (GDOP). The firmware must guarantee frequent readings, with an embedded pre-filter to reduce transmitted data volume without sacrificing quality, and reliable publishing even under unstable network conditions.

---

### Out of Scope (MVP)

The following are explicitly excluded from this EPIC:

- Active BLE scanning or scan request responses
- Scanning for more than one tag simultaneously
- RSSI-based filtering of non-Eddystone packets beyond UUID matching
- TLS for the local OTA HTTP server
- Cloud MQTT broker support
- Sub-millisecond timestamp precision

---

### Technical Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| BLE mode | Passive scan (no scan requests) | Anchors only receive advertising packets; active scan wastes energy and channel time with no benefit in the MVP |
| Tag identification | Filter by UUID `019d8ba3-5d5f-792b-8e60-906fbeca324a` (Namespace + Instance from Eddystone-UID) | Ensures only the authorized tag is processed; UUID is fixed in the MVP — see Tag module spec |
| Scan period | 200 ms continuous, scan window = scan interval | Maximum channel coverage; predictable detection latency; energy trade-off acceptable for mains-powered hardware |
| Publish trigger | One MQTT message per completed scan cycle | Guarantees ≥ 4 msg/s; no deduplication or change-threshold logic in the MVP |
| On-chip noise filter | EWA with alpha = 0.3 (configurable via `ANCHOR_EWA_ALPHA` in `anchor_config.h`) | Reduces standard deviation without introducing high latency; lower alpha → more smoothing, less responsiveness to fast changes |
| EWA formula | `rssi_f(t) = α × rssi_raw(t) + (1 − α) × rssi_f(t−1)` | Classic 1st-order IIR filter; low computational cost on ESP32-S3 |
| Timestamp resolution | Unix epoch in **milliseconds** via SNTP + `gettimeofday()` | At ≥ 4 msg/s, second-resolution timestamps are ambiguous; milliseconds allow unambiguous ordering and cross-anchor correlation |
| MQTT publish format | Flat JSON: `{"anchor_id":"A1","rssi":-67.3,"ts":1712345678123}` | Parseable by any backend; `ts` is millisecond epoch; `rssi` is the filtered float |
| MQTT topic | `indoor/anchor/{anchor_id}/rssi` | Standardized hierarchy; supports per-anchor subscription or wildcard `indoor/anchor/+/rssi` |
| MQTT QoS | QoS 0 for RSSI publications; QoS 1 for heartbeat and null-rssi | RSSI is high-frequency; dropped frames are tolerable. Heartbeat and signal-loss events are semantically important and low-frequency |
| MQTT retain | Disabled on RSSI topic; enabled on heartbeat topic | A new backend subscriber gets the last known anchor health state without waiting up to 10 s |
| MQTT client ID | `anchor_{ANCHOR_ID}` (e.g. `anchor_A1`) | Unique per device; prevents broker-side disconnect conflicts when multiple anchors connect simultaneously |
| WiFi/MQTT reconnection | Fixed 1 s retry backoff; up to 5 full sequences (WiFi → SNTP → MQTT) before watchdog-triggered reboot | Predictable behavior on prolonged failure; watchdog ensures auto-recovery without manual intervention |
| Time synchronization | SNTP after WiFi connection; millisecond resolution via `gettimeofday()` | Allows unambiguous ordering of ≥ 4 publications/s and cross-anchor timestamp correlation |
| Anchor identity | `ANCHOR_ID` defined via `#define` in `anchor_config.h` (e.g. `"A1"`) | A single firmware base compiles for all anchors; only `anchor_config.h` changes per unit |
| ESP-IDF version | v6.0 | Parity with Tag module; stable BLE, WiFi, and MQTT APIs on this version |
| MQTT security | Username/password authentication; TLS optional for production | Sufficient for lab LAN in MVP; TLS enabled by compile-time flag `ANCHOR_MQTT_TLS_ENABLED` in production build |
| OTA | Local HTTP (no TLS) + Safe OTA (2 app partitions + 1 OTA data) | Same strategy as Tag module; controlled internal network removes the need for TLS in MVP; automatic rollback on failure |
| OTA WiFi | Reuses `ANCHOR_WIFI_SSID` / `ANCHOR_WIFI_PASSWORD` | Single lab network in MVP; no separate OTA credentials needed |
| Watchdog | ESP-IDF Task Watchdog enabled for all FreeRTOS tasks | Guarantees recovery from any hung task (BLE scan, WiFi, MQTT publish, main loop); no selective monitoring in MVP |
| LED behavior | Connecting: slow blink (500 ms); connected + MQTT active: solid; OTA in progress: fast blink (100 ms); reboot imminent: 3 rapid blinks | Visual status without serial monitor; minimal state machine on the main task |

---

### Functional Requirements

| ID | Description | Priority |
|----|-------------|----------|
| RF-01 | The anchor must passively scan the BLE channel and record the RSSI of any advertising packet from the tag identified by UUID `019d8ba3-5d5f-792b-8e60-906fbeca324a`. The scan period is defined by the constant `ANCHOR_SCAN_INTERVAL_MS` (default: 200 ms). | Must Have |
| RF-02 | The firmware must apply an on-chip EWA filter to the raw RSSI before publishing. The alpha factor is defined by the constant `ANCHOR_EWA_ALPHA` (default: 0.3; valid range: 0.1–1.0). Formula: `rssi_f = α × rssi_raw + (1 − α) × rssi_prev`. On the first reading after boot or after signal loss (see RF-09), `rssi_prev` is initialized with `rssi_raw`. | Must Have |
| RF-03 | The anchor must publish one MQTT message per completed scan cycle. A scan cycle is considered complete after `ANCHOR_SCAN_INTERVAL_MS` has elapsed from the start of the previous cycle, regardless of whether a tag packet was received during that interval. Data must be published as flat JSON: `{"anchor_id":"A1","rssi":-67.3,"ts":1712345678123}`. The `ts` field is Unix epoch in milliseconds obtained via SNTP + `gettimeofday()`. The `rssi` field is the EWA-filtered value (float, 1 decimal place). There is no deduplication or change-threshold logic. | Must Have |
| RF-04 | The anchor must publish to the MQTT topic `indoor/anchor/{anchor_id}/rssi`, where `{anchor_id}` is the value of the `ANCHOR_ID` constant (e.g. `"A1"`, `"A2"`, `"A3"`). | Must Have |
| RF-05 | The firmware must automatically reconnect after a WiFi connection loss. One attempt consists of three sequential steps, each with its own timeout: (1) reconnect WiFi — waits up to `ANCHOR_WIFI_CONNECT_TIMEOUT_MS` (default: 10 000 ms); (2) sync SNTP — waits up to `ANCHOR_SNTP_SYNC_TIMEOUT_MS` (default: 5 000 ms); (3) reconnect MQTT — waits up to `ANCHOR_MQTT_CONNECT_TIMEOUT_MS` (default: 5 000 ms). If any step fails or times out, the full attempt is counted as failed. The backoff between attempts is fixed at 1 s. After 5 consecutive failed attempts, the watchdog reboots the device. A successful attempt resets the counter. | Must Have |
| RF-06 | The physical coordinates (x, y) of each anchor must be configurable via `anchor_config.h` through the constants `ANCHOR_POS_X` and `ANCHOR_POS_Y` (float, in meters). | Must Have |
| RF-07 | The anchor must publish a heartbeat to the topic `indoor/anchor/{anchor_id}/status` every `ANCHOR_HEARTBEAT_INTERVAL_S` seconds (default: 10 s) with the MQTT retain flag set. Payload: `{"anchor_id":"A1","uptime_s":3600,"wifi_rssi":-55,"free_heap":180000}`. | Should Have |
| RF-08 | The firmware must support OTA updates via WiFi for remote flashing. See sub-requirements RF-08a to RF-08d. | Should Have |
| RF-08a | OTA mode is activated by detecting a LOW level on the `ANCHOR_OTA_TRIGGER_GPIO` pin at boot (placeholder: GPIO 0 — confirm in schematic). While OTA mode is active, the LED blinks at 100 ms. | Should Have |
| RF-08b | WiFi credentials for OTA reuse `ANCHOR_WIFI_SSID` and `ANCHOR_WIFI_PASSWORD`. The OTA server URL is defined by `ANCHOR_OTA_SERVER_URL`. Timeouts: 30 s for WiFi connection; 60 s for full firmware download. On timeout or validation failure, the device reboots immediately with the previous firmware — no retry. | Should Have |
| RF-08c | The partition scheme must use Safe OTA (2 app partitions + 1 OTA data), enabling automatic rollback if the download or validation fails. The `partitions.csv` defining this scheme is a required deliverable of this EPIC. | Should Have |
| RF-08d | During OTA, BLE scanning and MQTT publishing are suspended. After a successful reboot, scanning and publishing resume normally. | Should Have |
| RF-09 | If the tag is not detected for more than `ANCHOR_TAG_TIMEOUT_MS` (default: 2000 ms), the anchor must publish `{"anchor_id":"A1","rssi":null,"ts":1712345678123}` to `indoor/anchor/{anchor_id}/rssi` at the normal scan cadence (one message per scan cycle) until the tag is detected again. `rssi` must be JSON null — not the string `"null"` and not a sentinel numeric value. Null-rssi messages must be published with QoS 1 (at-least-once delivery). When the tag is detected again, the EWA state resets: `rssi_prev` is re-initialized with the first new `rssi_raw` (see RF-02). | Must Have |
| RF-10 | The firmware must enable the ESP-IDF Task Watchdog for all FreeRTOS tasks. The timeout is defined by the constant `ANCHOR_WDT_TIMEOUT_SEC` (default: 10 s). Any task that fails to check in within this window triggers an automatic device reset. | Must Have |
| RF-11 | The LED (`ANCHOR_LED_GPIO`) must reflect device state: slow blink (500 ms on/off) while connecting to WiFi or MQTT; solid on when WiFi is connected and MQTT is active (i.e., after the CONNECT packet has been acknowledged by the broker); fast blink (100 ms) during OTA; 3 rapid blinks as a best-effort signal before a controlled reboot (e.g., after 5 failed reconnect attempts). This blink is implemented in a dedicated LED task; it will not occur if the device is reset directly by the Task Watchdog due to a hung task. | Should Have |

---

### `anchor_config.h` Constants Reference

All compile-time configuration for a given anchor unit is isolated in `anchor_config.h`. The table below is the authoritative list of constants. All constants must be present in the file; omitting one is a compile error. A stub file with placeholder values is a required deliverable of this EPIC.

| Constant | Type | Default / Placeholder | Notes |
|----------|------|-----------------------|-------|
| `ANCHOR_ID` | `const char*` | `"A1"` | One of `"A1"`, `"A2"`, `"A3"` |
| `ANCHOR_POS_X` | `float` | `0.0` | Meters; replace with surveyed value before field flash |
| `ANCHOR_POS_Y` | `float` | `0.0` | Meters; replace with surveyed value before field flash |
| `ANCHOR_WIFI_SSID` | `const char*` | — | Required; no default; used for both normal operation and OTA |
| `ANCHOR_WIFI_PASSWORD` | `const char*` | — | Required; no default; used for both normal operation and OTA |
| `ANCHOR_MQTT_HOST` | `const char*` | — | Required; no default |
| `ANCHOR_MQTT_PORT` | `uint16_t` | `1883` | Use `8883` when `ANCHOR_MQTT_TLS_ENABLED` is true |
| `ANCHOR_MQTT_USER` | `const char*` | — | Required; no default |
| `ANCHOR_MQTT_PASSWORD` | `const char*` | — | Required; no default |
| `ANCHOR_MQTT_TLS_ENABLED` | `bool` | `false` | Set `true` for production build |
| `ANCHOR_MQTT_CLIENT_ID` | `const char*` | `"anchor_A1"` | Must be unique per broker; pattern: `anchor_{ANCHOR_ID}` |
| `ANCHOR_NTP_SERVER` | `const char*` | `"pool.ntp.org"` | URL of NTP server |
| `ANCHOR_OTA_SERVER_URL` | `const char*` | — | Required if OTA is used; e.g. `http://192.168.1.100:8000/anchor.bin` |
| `ANCHOR_OTA_TRIGGER_GPIO` | `gpio_num_t` | `GPIO_NUM_0` | Placeholder; confirm in schematic |
| `ANCHOR_SCAN_INTERVAL_MS` | `uint32_t` | `200` | Milliseconds; scan window = scan interval |
| `ANCHOR_EWA_ALPHA` | `float` | `0.3` | Valid range: 0.1–1.0; firmware must enforce this with a compile-time `static_assert` |
| `ANCHOR_WIFI_CONNECT_TIMEOUT_MS` | `uint32_t` | `10000` | Per-attempt timeout for WiFi association step in reconnect sequence (RF-05) |
| `ANCHOR_SNTP_SYNC_TIMEOUT_MS` | `uint32_t` | `5000` | Per-attempt timeout for SNTP sync step in reconnect sequence (RF-05) |
| `ANCHOR_MQTT_CONNECT_TIMEOUT_MS` | `uint32_t` | `5000` | Per-attempt timeout for MQTT connection step in reconnect sequence (RF-05) |
| `ANCHOR_TAG_TIMEOUT_MS` | `uint32_t` | `2000` | Milliseconds before null-rssi is published |
| `ANCHOR_HEARTBEAT_INTERVAL_S` | `uint32_t` | `10` | Seconds between heartbeat publishes |
| `ANCHOR_WDT_TIMEOUT_SEC` | `uint32_t` | `10` | Task Watchdog timeout in seconds |
| `ANCHOR_LED_GPIO` | `gpio_num_t` | `GPIO_NUM_2` | Placeholder; confirm in schematic |

---

### BLE Mapping — Tag Identification

The anchor filters incoming BLE packets by the Namespace + Instance pair from the Tag module's Eddystone-UID format:

| Eddystone Field | Bytes | Value (UUID `019d8ba3-5d5f-792b-8e60-906fbeca324a`) |
|-----------------|-------|------------------------------------------------------|
| Namespace | 10 bytes | `01 9d 8b a3 5d 5f 79 2b 8e 60` |
| Instance | 6 bytes | `90 6f be ca 32 4a` |

> **Note:** The 2-byte `Reserved` field of the Eddystone payload is used by the Tag module as a `uint16_t` sequence counter with natural rollover (0xFFFF → 0x0000). The anchor must not treat the rollover as an error. No action is required on this field in the MVP.

---

### Hardware Placeholders

The values below are used in firmware during development. **They must be replaced with the definitive values from the schematic before the first field flash.**

| Constant | Placeholder | Required action |
|----------|-------------|-----------------|
| `ANCHOR_LED_GPIO` | GPIO 48 (onboard RGB LED on ESP32-S3-DevKitC-1) | Confirm pin in schematic. **Note:** the DevKitC-1 onboard LED is a WS2812 (NeoPixel) and requires the RMT peripheral — it cannot be driven by plain GPIO toggle. If a different board or external LED is used, a standard GPIO may apply |
| `ANCHOR_OTA_TRIGGER_GPIO` | GPIO 0 (BOOT button) | Confirm pin in schematic |
| `ANCHOR_POS_X` / `ANCHOR_POS_Y` | 0.0 / 0.0 | Measure and document actual position on floor plan before validation |

---

### Dependencies

| Dependency | Type | Notes |
|------------|------|-------|
| Tag module spec (EPIC-01) | Internal | UUID, Eddystone-UID format, and sequence counter behavior are defined in the Tag spec |
| MQTT broker | Infrastructure | Broker must be reachable on the local network; address via `ANCHOR_MQTT_HOST` / `ANCHOR_MQTT_PORT`; credentials via `ANCHOR_MQTT_USER` / `ANCHOR_MQTT_PASSWORD` |
| NTP server | Infrastructure | `pool.ntp.org` or local NTP for millisecond-resolution `ts` field; URL configurable via `ANCHOR_NTP_SERVER` |
| Local HTTP server for OTA | Infrastructure | Required to test RF-08; `python3 -m http.server 8000` on lab LAN (port 8000); no TLS in MVP |
| Environment floor plan | Hardware/Deploy | (x, y) coordinates for each anchor depend on a physical survey of the space |
| Electrical schematic | Hardware | Defines the definitive GPIOs; confirmation is a prerequisite for flashing production hardware (does not block development on a DevKit with placeholder values) |
| `anchor/specs/001-mvp/validation.md` | Internal deliverable | Created and maintained by the Hardware Team as part of this EPIC; documents EWA validation results and surveyed anchor coordinates |

---

### Non-Functional Requirements

| Guarantee | Acceptance Criterion |
|-----------|----------------------|
| Publish frequency | Minimum 4 MQTT publications per second per anchor under normal operating conditions |
| Publish latency | Time between RSSI scan and MQTT publish < 50 ms |
| Network resilience | Automatic reconnection to broker after WiFi drop within ~5 s (5 attempts × 1 s backoff), without manual intervention |
| Pre-filter accuracy | EWA reduces raw RSSI standard deviation by at least 30% in a static environment with the tag stationary |
| Availability | Continuous operation for at least 72 hours without a spontaneous reboot; Task Watchdog active for all tasks |
| MQTT security | Broker connection with username/password authentication; TLS enabled by `ANCHOR_MQTT_TLS_ENABLED` for production |
| Physical placement | Anchors installed at 2–2.5 m height, no metal obstacles within 30 cm radius; positions recorded on floor plan |
| Documentation | Source code commented in English; README with flash instructions, `anchor_config.h` configuration guide, and broker setup |

---

### Definition of Done (DoD)

- [ ] Firmware for all 3 anchors compiled without errors on ESP-IDF v6.0 with distinct IDs (`A1`, `A2`, `A3`)
- [ ] Firmware flashed and all 3 anchors operating simultaneously without conflicts
- [ ] MQTT publications verified in the broker using MQTT Explorer (valid JSON payload with `anchor_id`, `rssi`, `ts` fields; `ts` confirmed as millisecond epoch)
- [ ] Minimum 4 msg/s frequency confirmed by broker log analysis over 5 continuous minutes
- [ ] Resilience test: WiFi drop and recovery simulated; automatic reconnection validated within ~5 s for all 3 anchors
- [ ] EWA pre-filter validated: raw vs. filtered RSSI standard deviation measured and documented in `anchor/specs/001-mvp/validation.md` in a static environment (tag stationary, 5 minutes of data collection)
- [ ] Coordinates (x, y) for all 3 anchors configured in `anchor_config.h`, documented in `validation.md`, and mapped on the environment floor plan
- [ ] Heartbeat operating and visible in the broker for all 3 anchors with uptime incrementing correctly; retain flag confirmed (new subscriber receives last heartbeat immediately without waiting)
- [ ] Absence of tag detection results in `rssi: null` (JSON null) publications at scan cadence after 2 s timeout, validated by powering off the tag for 5 s
- [ ] OTA success path tested: new firmware flashed via HTTP trigger, device reboots into new version, MQTT publications resume normally
- [ ] OTA rollback tested: interrupted download results in reboot with previous firmware functional
- [ ] `partitions.csv` for Safe OTA (2 app partitions + 1 OTA data) committed to repository
- [ ] `anchor_config.h` stub (all constants with placeholder values) committed to repository
- [ ] Git tag `anchor-firmware-v1.0` created on the delivery commit

> **Note:** The 72-hour stability test is planned for Sprint 2, after the base firmware is delivered and validated in Sprint 1.

---
