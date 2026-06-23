#include "app_state.h"

void trimInPlace(char* s) {
  if (!s || !*s) return;

  size_t len = strlen(s);
  while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n' || s[len - 1] == ' ' || s[len - 1] == '\t')) {
    s[--len] = '\0';
  }

  size_t start = 0;
  while (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n') {
    start++;
  }

  if (start > 0) {
    memmove(s, s + start, strlen(s + start) + 1);
  }
}

bool isBlank(const char* s) {
  return (!s || s[0] == '\0');
}

bool readSerialLineBlocking(char* out, size_t outLen) {
  if (!out || outLen == 0) return false;
  out[0] = '\0';

  while (Serial.available() == 0) {
    delay(50);
  }

  size_t n = Serial.readBytesUntil('\n', out, outLen - 1);
  out[n] = '\0';
  trimInPlace(out);
  return true;
}

bool promptYesNoKeepCurrent(const __FlashStringHelper* prompt, bool currentValue, bool* outValue) {
  if (!outValue) return false;

  char line[SERIAL_LINE_MAX];
  Serial.println(prompt);

  for (;;) {
    if (!readSerialLineBlocking(line, sizeof(line))) return false;

    if (line[0] == '\0') {
      *outValue = currentValue;
      return true;
    }

    char c = line[0];
    if (c == 'y' || c == 'Y') {
      *outValue = true;
      return true;
    }
    if (c == 'n' || c == 'N') {
      *outValue = false;
      return true;
    }
    Serial.println(F("Please enter 'y' or 'n', or press Enter to keep current/default:"));
  }
}

void printIpDetails() {
  debugPrintln(F("\n=== Network Details ==="));
  debugPrint(F("SSID: ")); Serial.println(WiFi.SSID());
  debugPrint(F("Hostname: ")); Serial.println(WiFi.hostname());
  debugPrint(F("MAC: ")); Serial.println(WiFi.macAddress());
  debugPrint(F("RSSI (dBm): ")); Serial.println(WiFi.RSSI());

  debugPrint(F("Local IP: ")); Serial.println(WiFi.localIP());
  debugPrint(F("Subnet Mask: ")); Serial.println(WiFi.subnetMask());
  debugPrint(F("Gateway IP: ")); Serial.println(WiFi.gatewayIP());

  debugPrint(F("DNS1: ")); Serial.println(WiFi.dnsIP(0));
  debugPrint(F("DNS2: ")); Serial.println(WiFi.dnsIP(1));
  debugPrintln(F("========================\n"));
}

void printConfig() {
  debugPrintln(F("\n=== Current Configuration ==="));
  debugPrint(F("WiFi SSID: ")); Serial.println(wifi_ssid);
  debugPrint(F("MQTT Server: ")); Serial.print(mqtt_server); Serial.print(':'); Serial.println(mqtt_port);
  debugPrint(F("MQTT Connect Attempts: ")); Serial.println(mqtt_connect_attempts);
  debugPrint(F("Device ID: ")); Serial.println(device_id);
  debugPrint(F("Measurement Interval: ")); Serial.print(interval_seconds); Serial.println(F(" seconds"));
  debugPrint(F("Sensor Offset: ")); Serial.print(sensor_offset_cm, 2); Serial.println(F(" cm"));
  debugPrint(F("Sensor Mode: ")); Serial.println(sensorModeName(sensor_mode));
  debugPrint(F("Tank Height: ")); Serial.print(tank_height_cm, 2); Serial.println(F(" cm"));
  debugPrint(F("Deep Sleep: ")); Serial.println(enable_deep_sleep ? F("ON") : F("OFF"));
  debugPrint(F("Production Mode: ")); Serial.println(production_mode ? F("ON") : F("OFF"));
  debugPrint(F("Stored Log Level: ")); Serial.println(logLevelName(stored_log_level));
  debugPrint(F("Active Log Level: ")); Serial.println(logLevelName((uint8_t)currentLogLevel));
  debugPrint(F("NTP Sync: ")); Serial.println(ntp_enabled ? F("ON") : F("OFF"));
  debugPrint(F("NTP Server: ")); Serial.println(ntp_server);
  debugPrint(F("Last Depth: ")); Serial.print(last_depth_measured, 1); Serial.println(F(" cm"));
  debugPrint(F("Running Avg: ")); Serial.print(running_avg_value, 1); Serial.println(F(" cm"));
  debugPrint(F("Battery Voltage: ")); Serial.print(last_battery_voltage, 2); Serial.println(F(" V"));
  debugPrintln(F("============================\n"));
}

void printMenuLine(const char* left, const char* right) {
  const int leftWidth = 42;
  char line[160];
  snprintf(line, sizeof(line), "%-*s | %s", leftWidth, left, right);
  debugPrintln(line);
}

void showMenu() {
  char right[RIGHT_COL_MAX];

  debugPrintln(F("\n=== Water Level Meter Menu ==="));

  snprintf(right, sizeof(right), "WiFi SSID: %s", wifi_ssid);
  printMenuLine("c: Clear config file (/config.json)", right);

  snprintf(right, sizeof(right), "MQTT Server: %s", mqtt_server);
  printMenuLine("d: Toggle deep sleep on/off", right);

  snprintf(right, sizeof(right), "MQTT Port: %d", mqtt_port);
  printMenuLine("e: Clear saved settings and reboot", right);

  snprintf(right, sizeof(right), "Calibrate sensor offset and tank height");
  printMenuLine("v: Calibrate sensor/tank", right);

  snprintf(right, sizeof(right), "Stored Log: %s", logLevelName(stored_log_level));
  printMenuLine("g: Cycle log level (ERROR/INFO/DEBUG)", right);

  snprintf(right, sizeof(right), "Production Mode: %s", production_mode ? "ON" : "OFF");
  printMenuLine("h: Toggle production mode on/off", right);

  snprintf(right, sizeof(right), "Sensor offset: %.2f cm", sensor_offset_cm);
  printMenuLine("i: Show IP/network details", right);

  snprintf(right, sizeof(right), "Deep Sleep: %s", enable_deep_sleep ? "ON" : "OFF");
  printMenuLine("k: Clear MQTT cache file (/cache.txt)", right);

  snprintf(right, sizeof(right), "Active Log: %s", logLevelName((uint8_t)currentLogLevel));
  printMenuLine("m: Show this menu (waits 5s)", right);

  snprintf(right, sizeof(right), "NTP: %s", ntp_enabled ? "ON" : "OFF");
  printMenuLine("n: Toggle NTP sync on/off", right);

  snprintf(right, sizeof(right), "NTP Server: %s", ntp_server);
  printMenuLine("p: Print current configuration", right);

  snprintf(right, sizeof(right), "Last Depth: %.1f cm", last_depth_measured);
  printMenuLine("r: Reboot device", right);

  snprintf(right, sizeof(right), "Sensor Mode: %s", sensorModeName(sensor_mode));
  printMenuLine("s: Edit current settings", right);

  snprintf(right, sizeof(right), "Tank: %.0f %%", last_tank_percent);
  printMenuLine("t: Show tank level", right);

  snprintf(right, sizeof(right), "Device ID: %s", device_id);
  printMenuLine("", right);

  snprintf(right, sizeof(right), "Battery: %.2f V (%d%%)", last_battery_voltage, (int)last_battery_percent);
  printMenuLine("", right);

  debugPrintln(F("============================="));
}

bool waitForSerialInput(uint32_t timeoutMs) {
  uint32_t start = millis();
  while ((millis() - start) < timeoutMs) {
    if (Serial.available() > 0) return true;
    delay(10);
  }
  return false;
}

void showMenuWithTimeout(uint32_t timeoutMs) {
  showMenu();
  debugPrint(F("[MENU] Press a key within "));
  Serial.print(timeoutMs / 1000);
  Serial.println(F("s to run a command, otherwise continuing...\n"));

  if (waitForSerialInput(timeoutMs)) {
    handleSerialMenu();
  } else {
    debugPrintln(F("[MENU] No input, continuing.\n"));
  }
}

void waitForNextMeasurement(uint32_t timeoutMs) {
  if (timeoutMs == 0) return;

  if (production_mode) {
    delay(timeoutMs);
    return;
  }

  debugPrint(F("[WAIT] Waiting "));
  Serial.print(timeoutMs / 1000);
  Serial.println(F(" seconds until the next measurement."));

  uint32_t shownMenuMs = min(timeoutMs, (uint32_t)10000);
  showMenuWithTimeout(shownMenuMs);

  uint32_t remainingMs = (timeoutMs > shownMenuMs) ? (timeoutMs - shownMenuMs) : 0;
  while (remainingMs > 0) {
    uint32_t chunk = min(remainingMs, (uint32_t)1000);
    delayWithSerial(chunk);
    remainingMs -= chunk;
  }
}

void delayWithSerial(uint32_t ms) {
  uint32_t start = millis();
  while ((millis() - start) < ms) {
    if (!production_mode && Serial.available() > 0) {
      handleSerialMenu();
      return;
    }
    delay(10);
  }
}

void handleSerialMenu() {
  if (!Serial.available()) return;

  char cmd = (char)Serial.read();
  switch (cmd) {
    case 'r':
      infoPrintln(F("Rebooting..."));
      delay(500);
      ESP.restart();
      break;

    case 'c':
      infoPrintln(F("Clearing /config.json and rebooting..."));
      LittleFS.remove(CONFIG_FILE);
      delay(500);
      ESP.restart();
      break;

    case 'k':
      infoPrintln(F("Clearing MQTT cache file (/cache.txt)..."));
      if (LittleFS.exists(CACHE_FILE)) {
        LittleFS.remove(CACHE_FILE);
        infoPrintln(F("Cache cleared."));
      } else {
        infoPrintln(F("No cache file found."));
      }
      break;

    case 'p':
      printConfig();
      break;

    case 's':
      promptForConfigEdit();
      break;

    case 'e':
      infoPrintln(F("Clearing saved settings and rebooting..."));
      LittleFS.remove(CONFIG_FILE);
      LittleFS.remove(CACHE_FILE);
      delay(500);
      ESP.restart();
      break;

    case 'm':
      showMenuWithTimeout(5000);
      break;

    case 'd':
      enable_deep_sleep = !enable_deep_sleep;
      saveConfigAndPublish();
      infoPrint(F("[CONFIG] Deep sleep is now "));
      Serial.println(enable_deep_sleep ? F("ON") : F("OFF"));
      break;

    case 'g':
      cycleStoredLogLevel();
      saveConfigAndPublish();
      infoPrint(F("[LOG] Stored log level is now "));
      Serial.println(logLevelName(stored_log_level));
      infoPrint(F("[LOG] Active log level is now "));
      Serial.println(logLevelName((uint8_t)currentLogLevel));
      break;

    case 'h':
      production_mode = !production_mode;
      updateLogLevel();
      saveConfigAndPublish();
      if (!production_mode) {
        infoPrint(F("[LOG] Production mode is now "));
        Serial.println(F("OFF"));
        infoPrint(F("[LOG] Active log level is now "));
        Serial.println(logLevelName((uint8_t)currentLogLevel));
      }
      break;

    case 'v':
      infoPrintln(F("Starting calibration routine..."));
      calibrateTankAndOffset();
      break;

    case 'n':
      ntp_enabled = !ntp_enabled;
      saveConfigAndPublish();
      infoPrint(F("[CONFIG] NTP sync is now "));
      Serial.println(ntp_enabled ? F("ON") : F("OFF"));
      break;

    case 'i':
      if (WiFi.status() == WL_CONNECTED) {
        printIpDetails();
      } else {
        errorPrintln(F("[WIFI] Not connected."));
      }
      break;

    case 't':
      infoPrint(F("[TANK] Current level: "));
      Serial.print(last_tank_percent, 0);
      Serial.println(F("%"));
      break;

    default:
      infoPrintln(F("Unknown command. Press 'm' for menu."));
      break;
  }
}
