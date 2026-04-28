# Validation Report — Tag Firmware v1.0

## Status

> Pending hardware measurements. Template created; fill in after bench/field testing.

---

## 1. Current Draw

| Condition | Value |
|-----------|-------|
| Average current (advertising at 100 ms) | ___ mA |
| Peak current | ___ mA |
| Measurement method | Series 1 Ω shunt + multimeter in DC current mode, or USB power meter. Steady-state ≥ 30 s running. |

**Battery life estimate:** 500 mAh / ___ mA = ___ hours  
Requirement: ≥ 8 h

---

## 2. Packet Loss

| Parameter | Value |
|-----------|-------|
| Capture duration | 1000 expected events at 100 ms interval |
| Distance | 10 m, indoor LOS (no obstructions) |
| Packets received | ___ |
| Packets lost | ___ |
| Loss rate | ___% |
| Tool | nRF Sniffer + Wireshark |

Requirement: < 2%

---

## 3. Boot-to-First-Advertising Latency

| Sample | Latency (ms) |
|--------|-------------|
| 1 | ___ |
| 2 | ___ |
| 3 | ___ |
| 4 | ___ |
| 5 | ___ |
| **Min** | ___ |
| **Max** | ___ |
| **Avg** | ___ |

Measurement: logic analyser trigger on VCC → first Coded PHY packet on nRF Sniffer.  
Requirement: < 2000 ms

---

## 4. Hardware Compatibility

| Board | BLE Advertising Smoke Test |
|-------|---------------------------|
| ESP32-WROOM-32 | ☐ Pass / ☐ Fail |
| ESP32-WROVER | ☐ Pass / ☐ Fail |

Notes (e.g. WROVER PSRAM heap impact on NimBLE stack):

---

## 5. Open Items

- **24h stability test** — deferred to Sprint 2 per spec. Not a blocker for `tag-firmware-v1.0`.
- **`BLE_TX_POWER_1M_DBM`** — placeholder -65 dBm. Measure at 1 m before first field flash.
- **GPIO confirmation** — `LED_STATUS_GPIO` (2) and `OTA_TRIGGER_GPIO` (0) are placeholders. Confirm from schematic before field flash.
