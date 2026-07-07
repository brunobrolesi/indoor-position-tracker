# Tag Firmware — ESP32 BLE Indoor Positioning Tag

BLE Extended Advertising tag (Eddystone-UID, Coded PHY S=8) for the indoor positioning system MVP.

## Prerequisites

- ESP-IDF v6.0 (with NimBLE and Extended Advertising support)
- ESP32-S3 development board with BLE 5 extended advertising support
- BLE 5 scanner/sniffer with Coded PHY support to verify BLE output

## Configuration

All tunable parameters are in `main/tag_config.h`:

| Constant | Default | Notes |
|----------|---------|-------|
| `BLE_ADV_INTERVAL_MS` | 100 | Valid: 50–500 ms. Not runtime-configurable. |
| `BLE_TX_POWER_DBM` | 9 | Radio TX power in dBm. Valid: -12 to +9. |
| `BLE_TX_POWER_1M_DBM` | -71 | **TODO: measure at 1 m before field flash.** |
| `LED_STATUS_GPIO` | 48 | Onboard WS2812B RGB LED on ESP32-S3-DevKitC-1. |

## Build and Flash

```bash
# Activate ESP-IDF environment (adjust path as needed)
. $HOME/esp/esp-idf/export.sh

cd tag/firmware

# Set target
idf.py set-target esp32s3

# Build
idf.py build

# Flash (replace /dev/cu.usbserial-0001 with your port)
idf.py -p /dev/cu.usbserial-0001 flash monitor
```

## Verifying BLE Output

1. Open a BLE 5 Coded PHY capable scanner/sniffer.
2. Filter by service UUID `0xFEAA` (Eddystone).
3. Inspect the advertising payload and verify:
   - **Namespace**: `01 9D 8B A3 5D 5F 79 2B 8E 60`
   - **Instance**: `90 6F BE CA 32 4A`
   - **Sequence counter** (Reserved bytes): increments every 100 ms
4. Confirm **Coded PHY S=8** in the scanner/sniffer PHY details.

## Partition Layout

The tag uses the default ESP-IDF single-app partition layout for the MVP.
