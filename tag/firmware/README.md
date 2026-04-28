# Tag Firmware — ESP32 BLE Indoor Positioning Tag

BLE Extended Advertising tag (Eddystone-UID, Coded PHY S=8) for the indoor positioning system MVP.

## Prerequisites

- ESP-IDF v6.0 (with NimBLE and Extended Advertising support)
- ESP32-WROOM-32 or ESP32-WROVER development board
- nRF Connect (mobile or desktop) to verify BLE output

## Configuration

All tunable parameters are in `main/tag_config.h`:

| Constant | Default | Notes |
|----------|---------|-------|
| `BLE_ADV_INTERVAL_MS` | 100 | Valid: 50–500 ms. Not runtime-configurable. |
| `BLE_TX_POWER_DBM` | 0 | Radio TX power in dBm. Valid: -12 to +9. |
| `BLE_TX_POWER_1M_DBM` | -65 | **TODO: measure at 1 m before field flash.** |
| `LED_STATUS_GPIO` | 2 | **TODO: confirm from schematic.** |
| `OTA_TRIGGER_GPIO` | 0 | **TODO: confirm from schematic.** |
| `OTA_WIFI_SSID` | `CHANGE_ME` | WiFi SSID for OTA network. |
| `OTA_WIFI_PASSWORD` | `CHANGE_ME` | WiFi password for OTA network. |
| `OTA_SERVER_URL` | `http://192.168.1.100:8070/tag-firmware.bin` | HTTP OTA server URL. |

## Build and Flash

```bash
# Activate ESP-IDF environment (adjust path as needed)
. $HOME/esp/esp-idf/export.sh

cd tag/firmware

# Set target
idf.py set-target esp32

# Build
idf.py build

# Flash (replace /dev/cu.usbserial-0001 with your port)
idf.py -p /dev/cu.usbserial-0001 flash monitor
```

## OTA Update

1. Start a local HTTP server serving the firmware binary:

```bash
cd tag/firmware/build
python3 -m http.server 8070
```

2. Update `OTA_SERVER_URL` in `tag_config.h` to point to your machine's IP.
3. Update `OTA_WIFI_SSID` and `OTA_WIFI_PASSWORD` with the lab network credentials.
4. Hold the BOOT button (GPIO 0) during power-on to enter OTA mode.
5. Monitor via `idf.py monitor` — the device will reboot into new firmware on success, or back to the previous firmware on failure.

## Verifying with nRF Connect

1. Open nRF Connect → Scanner → Start scan.
2. Filter by service UUID `0xFEAA` (Eddystone).
3. Tap the device to expand — verify:
   - **Namespace**: `01 9D 8B A3 5D 5F 79 2B 8E 60`
   - **Instance**: `90 6F BE CA 32 4A`
   - **Sequence counter** (Reserved bytes): increments every 100 ms
4. If your scanner supports PHY display, confirm **Coded PHY S=8**.

## Partition Layout

```
nvs       0x9000   24 KB
otadata   0xf000    8 KB
phy_init  0x11000   4 KB
ota_0     0x20000   1.5 MB
ota_1     0x1A0000  1.5 MB
```
