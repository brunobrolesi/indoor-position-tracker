# Anchor Firmware — ESP32-S3 Indoor Positioning (MVP)

Firmware for the three fixed anchor nodes. Each anchor passively scans BLE for the authorized Eddystone-UID tag, applies an on-chip EWA filter to the RSSI, and publishes readings to an MQTT broker at ≥ 4 msg/s.

---

## Prerequisites

- **ESP-IDF v6.0** installed and activated (`source ~/.espressif/v6.0/esp-idf/export.sh`)
- **Python 3** (for OTA HTTP server)
- **MQTT broker** reachable on the lab LAN (e.g. Mosquitto)
- **MQTT Explorer** or `mosquitto_sub` to verify publications

---

## Per-anchor configuration

Edit the unit-specific config file before flashing:

- `main/anchor_config_A1.h` — anchor A1
- `main/anchor_config_A2.h` — anchor A2
- `main/anchor_config_A3.h` — anchor A3

Required fields to update (marked `CHANGE_ME`):

```c
#define ANCHOR_WIFI_SSID               "your-ssid"
#define ANCHOR_WIFI_PASSWORD           "your-password"
#define ANCHOR_MQTT_HOST               "192.168.x.x"
#define ANCHOR_MQTT_USER               "your-mqtt-user"
#define ANCHOR_MQTT_PASSWORD           "your-mqtt-password"
#define ANCHOR_OTA_SERVER_URL          "http://192.168.x.x:8000/anchor.bin"
```

Update `ANCHOR_POS_X` / `ANCHOR_POS_Y` with the surveyed anchor coordinates (meters) before field flash.

---

## Build and flash

```bash
# A1 (default)
idf.py -DANCHOR_UNIT=A1 build
idf.py -p /dev/cu.usbserial-XXXX flash monitor

# A2
idf.py -DANCHOR_UNIT=A2 build
idf.py -p /dev/cu.usbserial-XXXX flash monitor

# A3
idf.py -DANCHOR_UNIT=A3 build
idf.py -p /dev/cu.usbserial-XXXX flash monitor
```

---

## OTA update

1. Build the new firmware: `idf.py -DANCHOR_UNIT=A1 build`
2. Start an HTTP server in the build directory:
   ```bash
   cd build && python3 -m http.server 8000
   ```
3. Copy `anchor_firmware.bin` to a stable filename:
   ```bash
   cp build/anchor_firmware.bin build/anchor.bin
   ```
4. Hold the BOOT button (GPIO 0) and power-cycle the anchor. The LED blinks blue during download.
5. On success, the device reboots into the new firmware. MQTT publications resume automatically.

---

## Verify with MQTT

```bash
# Watch RSSI publications
mosquitto_sub -h 192.168.x.x -u user -P pass -t 'indoor/anchor/+/rssi' -v

# Watch heartbeat (should arrive every 10 s)
mosquitto_sub -h 192.168.x.x -u user -P pass -t 'indoor/anchor/+/status' -v

# Measure publish rate (expect ≥ 20 msgs / 5 s)
mosquitto_sub -h 192.168.x.x -u user -P pass -t 'indoor/anchor/A1/rssi' | pv -l -i 5
```

---

## LED status reference

| Color / Pattern | State |
|-----------------|-------|
| Amber slow blink (500 ms) | Connecting to WiFi or MQTT |
| Green solid | Connected and publishing |
| Blue fast blink (100 ms) | OTA download in progress |
| Red 3 rapid blinks | Controlled reboot (5 failed reconnects) |

---

## Notes

- **Concurrent BLE + WiFi**: requires coexistence flags in `sdkconfig.defaults` (`CONFIG_ESP_COEX_ENABLED`, `CONFIG_SW_COEXIST_ENABLE`). Validate during T0 spike.
- **MQTT component**: requires managed component `espressif/mqtt ^1.0.0` (not built-in in IDF v6.0). Run `idf.py update-dependencies` to fetch it.
- **WDT**: all FreeRTOS tasks (main, nimble_host, led_task) are registered with the Task Watchdog. Timeout: 10 s.
