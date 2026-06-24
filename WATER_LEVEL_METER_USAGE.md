# ESP8266 Water Level Meter – User & Configuration Guide


## Overview
This utility is a complete solution for monitoring water tank levels using an ESP8266 microcontroller and a waterproof ultrasonic sensor. It supports both:
- AJ-SR04M TRIG/ECHO modules
- SR04M-2 UART modules (5V, RX, TX, GND)

It is designed for:
- Easy installation and configuration
- Reliable, low-power operation (deep sleep between measurements)
- Automatic data upload to a server via MQTT (compatible with Node-Red, InfluxDB, etc.)
- Simple reconfiguration without re-flashing the device

---

## Quick Start
1. **Hardware Required:**
  - ESP8266 board (e.g., NodeMCU, Wemos D1 Mini)
  - Waterproof ultrasonic sensor (AJ-SR04M TRIG/ECHO or SR04M-2 UART)
  - Jumper wires
  - 5V to 3.3V logic level shifter or resistor divider for any sensor output going into ESP8266 RX/input pin
  - USB cable for programming

2. **Wiring (Mode 0: AJ-SR04M TRIG/ECHO):**
  - AJ-SR04M VCC → 5V on ESP8266
  - AJ-SR04M GND → GND on ESP8266
  - AJ-SR04M TRIG → D2 (GPIO4) on ESP8266
  - AJ-SR04M ECHO → D1 (GPIO5) on ESP8266

> **Important:** If the AJ-SR04M ECHO output is 5V, use a resistor divider or logic level shifter before connecting it to ESP8266 D1. The ESP8266 is not 5V tolerant.

3. **Wiring (Mode 1: SR04M-2 UART):**
  - SR04M-2 5V → 5V on ESP8266
  - SR04M-2 GND → GND on ESP8266
  - SR04M-2 RX → D2 (GPIO4) on ESP8266 (TX from ESP8266 software serial)
  - SR04M-2 TX → D1 (GPIO5) on ESP8266 (RX to ESP8266 software serial)

> **Important:** SR04M-2 TX may be 5V logic. Use a level shifter or resistor divider from sensor TX to ESP8266 D1.

4. **Flashing the Code:**
  - Use PlatformIO (recommended) or Arduino IDE to upload the code to your ESP8266.
  - Ensure all required libraries are installed (see platformio.ini).

5. **First Boot & Configuration:**
  - Open a serial monitor at 115200 baud.
  - On first boot, the device will prompt you to enter:
    - WiFi SSID
    - WiFi password
    - MQTT server address
    - MQTT port (default: 1883)
    - MQTT connect attempts (default: 10)
    - NTP time sync enabled (y/n)
    - NTP server (default: pool.ntp.org)
    - Timezone (default: Australia/Sydney)
    - Device ID (unique for each tank/device)
    - Sensor offset (cm)
    - Sensor mode (`0` = TRIG/ECHO, `1` = UART SR04M-2)
    - Measurement interval (seconds)
  - Enter each value as prompted. The device will save the settings and reboot.

6. **Operation:**
  - The device will connect to WiFi and MQTT, read multiple distance values from the selected sensor mode, average them (with outlier rejection), compute water depth and percent-full, and publish telemetry to MQTT.
  - If MQTT is unavailable, readings are cached and sent when connectivity is restored.
  - The device enters deep sleep between measurements to save power.

---


## Configuration Storage
- **All runtime settings**: `/config.json` on the ESP8266's LittleFS filesystem
- **Unsent readings**: `/cache.txt` (LittleFS), sent when MQTT is available

---


## Changing Settings (Reconfiguration)

### Option 1: Serial Reconfiguration
- Use the serial menu option `s` to edit current settings interactively.
- The device will prompt for each setting, and leaving an input blank keeps the current value.

### Option 2: Reset and Reconfigure
- Delete `/config.json` using a LittleFS tool or code.
- On next boot, the device will prompt for all required settings via Serial.

### Option 3: Edit Config File
- Use a LittleFS tool (e.g., PlatformIO LittleFS Data Upload, or a web uploader sketch) to edit `/config.json` directly.
- Example `/config.json`:
  ```json
  {
    "wifi_ssid": "yourSSID",
    "wifi_password": "yourPassword",
    "mqtt_server": "192.168.1.100",
    "mqtt_port": 1883,
    "mqtt_connect_attempts": 10,
    "device_id": "TANK1",
    "sensor_offset_cm": 0.0,
    "sensor_mode": 0,
    "tank_height_cm": 100.0,
    "interval_seconds": 600,
    "enable_deep_sleep": true,
    "ntp_enabled": true,
    "ntp_server": "pool.ntp.org",
    "timezone": "Australia/Sydney"
  }
  ```
- Save and reboot the device.

---


## MQTT Data Format
- **Topic:** `waterlevel/<device_id>/data` (e.g., `waterlevel/TANK1/data`)
- **Payload:** JSON with the following fields:
  - `device_id`: device identifier string
  - `distance_cm`: raw measured distance from sensor to water surface (cm)
  - `depth_cm`: computed water depth in cm (from the full-water surface)
  - `battery_v`: measured battery voltage (V)
  - `battery_pct`: battery charge percent (0-100)
  - `tank_pct`: percent full of the tank (0-100)

  Example payload:

  ```json
  {
    "device_id": "TANK1",
    "distance_cm": 45,
    "depth_cm": 57.5,
    "battery_v": 3.92,
    "battery_pct": 78,
    "tank_pct": 58
  }
  ```

---


## Troubleshooting
- If the device does not connect, check WiFi/MQTT settings in `/config.json`.
- If you see repeated prompts on Serial, config is missing or incomplete.
- If readings are not sent, check MQTT broker and network/firewall settings.
- If sensor readings are erratic, check wiring, sensor placement (avoid turbulence directly under the sensor), and ensure the correct `sensor_mode` is configured.
- If using UART mode and all readings are zero, verify:
  - Sensor TX is connected to D1 through level shifting
  - Sensor RX is connected to D2
  - Common ground between sensor and ESP8266
- If you want lower serial/log overhead, use `production_mode: true` or set `log_level` to `0` (ERROR) in `/config.json` or via the serial menu.

---

## MQTT Control Topics
- Settings publish topic: `waterlevel/<device_id>/settings`
- Remote config set topic: `waterlevel/<device_id>/config/set`
- Remote config get topic: `waterlevel/<device_id>/config/get`
- Remote config ack topic: `waterlevel/<device_id>/config/ack`

---


## Sensor Modes

### Mode 0: AJ-SR04M TRIG/ECHO
- Triggered with a 10 us pulse on TRIG.
- Echo pulse width is measured on ECHO.
- Pins: D2 = TRIG, D1 = ECHO.

### Mode 1: SR04M-2 UART
- Firmware sends UART trigger command and parses distance response frame.
- Pins: D2 = UART TX to sensor RX, D1 = UART RX from sensor TX.
- UART baud: 9600.

### Selecting Sensor Mode
- In serial setup/edit prompts, set `sensor_mode` to:
  - `0` for TRIG/ECHO
  - `1` for UART SR04M-2
- In JSON config, set:
  - `"sensor_mode": 0` or `"sensor_mode": 1`

---


## Advanced
- You can add more parameters to `/config.json` as needed (e.g., InfluxDB, Node-Red endpoints).
- For web-based config, add a captive portal or web server to the code.
- For multiple tanks, use a unique device ID for each ESP8266.

---

## Recommended Flow Transfer Workflow (Local <-> CentOS)
- Keep `flows.json` in this project as the single source of truth.
- Use the local check before deploy:

  ```powershell
  ./scripts/validate-flow.ps1
  ```

- On the CentOS server, pull and restart Node-RED with:

  ```bash
  ./scripts/deploy-centos.sh /home/<user>/WaterLevelMetre main nodered
  ```

- If Node-RED runs in a container, pass the container name as the third argument instead of a systemd service name:

  ```bash
  ./scripts/deploy-centos.sh /home/<user>@ourhome.local/WaterLevelMetre main node-red
  ```

- If the container uses a Node-RED project checkout inside `/data/projects/<project>`, the deploy script updates the host repo checkout and then copies the canonical `flows.json` into the in-container project before restarting the container. You can override the in-container project path with a fourth argument when needed.

- The flow currently expects these MQTT topics:
  - telemetry in: `waterlevel/+/data`
  - settings in: `waterlevel/+/settings`
  - config ack in: `waterlevel/+/config/ack`
  - config set out: `waterlevel/<tank>/config/set`
  - config get out: `waterlevel/<tank>/config/get`

---


## Resetting the Device
- To reset all settings, delete `/config.json` (can be done in code or with a tool).

---

## Support
For further help, consult the code comments, this document, or ask your project maintainer.