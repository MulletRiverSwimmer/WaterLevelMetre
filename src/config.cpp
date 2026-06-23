#include "app_state.h"

bool readConfigFromFS() {
  if (!LittleFS.exists(CONFIG_FILE)) return false;

  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f) return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  strlcpy(wifi_ssid, doc["wifi_ssid"] | "", sizeof(wifi_ssid));
  strlcpy(wifi_password, doc["wifi_password"] | "", sizeof(wifi_password));
  strlcpy(mqtt_server, doc["mqtt_server"] | "", sizeof(mqtt_server));
  strlcpy(device_id, doc["device_id"] | "", sizeof(device_id));

  mqtt_port = doc["mqtt_port"] | 1883;
  mqtt_connect_attempts = doc["mqtt_connect_attempts"] | 10;
  sensor_offset_cm = doc["sensor_offset_cm"] | 0.0f;
  interval_seconds = doc["interval_seconds"] | 600;
  tank_height_cm = doc["tank_height_cm"] | 100.0f;
  enable_deep_sleep = doc["enable_deep_sleep"] | false;
  production_mode = doc["production_mode"] | false;
  ntp_enabled = doc["ntp_enabled"] | true;
  strlcpy(ntp_server, doc["ntp_server"] | "pool.ntp.org", sizeof(ntp_server));

  if (doc["sensor_mode"].is<int>() || doc["sensor_mode"].is<uint8_t>()) {
    int m = doc["sensor_mode"] | SENSOR_MODE_TRIG_ECHO;
    sensor_mode = (m == SENSOR_MODE_UART) ? SENSOR_MODE_UART : SENSOR_MODE_TRIG_ECHO;
  } else if (doc["sensor_mode"].is<const char*>()) {
    uint8_t parsed = SENSOR_MODE_TRIG_ECHO;
    if (parseSensorMode(doc["sensor_mode"], &parsed)) {
      sensor_mode = parsed;
    }
  }

  last_depth_measured = doc["last_depth_measured"] | 0.0f;
  running_avg_value = doc["running_avg_value"] | 0.0f;

  if (doc["log_level"].is<uint8_t>() || doc["log_level"].is<int>()) {
    stored_log_level = doc["log_level"] | LOG_DEBUG;
  } else {
    bool old_debug_mode = doc["debug_mode"] | true;
    stored_log_level = old_debug_mode ? LOG_DEBUG : LOG_INFO;
  }

  updateLogLevel();

  if (tank_height_cm > 0.0f) {
    float pct = (last_depth_measured / tank_height_cm) * 100.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    last_tank_percent = pct;
  } else {
    last_tank_percent = 0.0f;
  }

  return strlen(wifi_ssid) > 0 &&
         strlen(wifi_password) > 0 &&
         strlen(mqtt_server) > 0 &&
         strlen(device_id) > 0;
}

void writeConfigToFS() {
  infoPrintln(F("[CONFIG] Writing config to LittleFS..."));
  JsonDocument doc;
  doc["wifi_ssid"] = wifi_ssid;
  doc["wifi_password"] = wifi_password;
  doc["mqtt_server"] = mqtt_server;
  doc["mqtt_port"] = mqtt_port;
  doc["mqtt_connect_attempts"] = mqtt_connect_attempts;
  doc["sensor_offset_cm"] = sensor_offset_cm;
  doc["interval_seconds"] = interval_seconds;
  doc["device_id"] = device_id;
  doc["enable_deep_sleep"] = enable_deep_sleep;
  doc["production_mode"] = production_mode;
  infoPrint(F("[CONFIG] Writing production_mode: "));
  Serial.println(production_mode);
  doc["log_level"] = stored_log_level;
  doc["ntp_enabled"] = ntp_enabled;
  doc["ntp_server"] = ntp_server;
  doc["sensor_mode"] = sensor_mode;
  doc["last_depth_measured"] = last_depth_measured;
  doc["running_avg_value"] = running_avg_value;
  doc["tank_height_cm"] = tank_height_cm;
  doc["debug_mode"] = (stored_log_level == LOG_DEBUG);

  File f = LittleFS.open(CONFIG_FILE, "w");
  if (f) {
    serializeJsonPretty(doc, f);
    f.close();
    infoPrintln(F("[CONFIG] Config written successfully"));
  } else {
    errorPrintln(F("[CONFIG] Failed to open config file for writing"));
  }
}

void saveConfigAndPublish() {
  infoPrintln(F("[CONFIG] saveConfigAndPublish() called"));
  writeConfigToFS();
  if (client.connected()) {
    publishSettingsToMqtt();
  } else {
    infoPrintln(F("[MQTT] Settings saved locally; will publish after reconnect."));
  }
}

void promptForFSConfig() {
  char line[SERIAL_LINE_MAX];

  debugPrintln(F("\nConfig not found. Enter WiFi SSID:"));
  while (Serial.available()) Serial.read();
  readSerialLineBlocking(line, sizeof(line));
  strlcpy(wifi_ssid, line, sizeof(wifi_ssid));

  debugPrintln(F("Enter WiFi password:"));
  readSerialLineBlocking(line, sizeof(line));
  strlcpy(wifi_password, line, sizeof(wifi_password));

  debugPrintln(F("Enter MQTT server address:"));
  readSerialLineBlocking(line, sizeof(line));
  strlcpy(mqtt_server, line, sizeof(mqtt_server));

  debugPrintln(F("Enter MQTT port (default 1883):"));
  readSerialLineBlocking(line, sizeof(line));
  mqtt_port = atoi(line);
  if (mqtt_port == 0) mqtt_port = 1883;

  bool yn = ntp_enabled;
  if (promptYesNoKeepCurrent(F("Use NTP time sync? (y/n):"), ntp_enabled, &yn)) {
    ntp_enabled = yn;
  }

  debugPrintln(F("Enter NTP server (default pool.ntp.org):"));
  readSerialLineBlocking(line, sizeof(line));
  if (!isBlank(line)) strlcpy(ntp_server, line, sizeof(ntp_server));

  debugPrintln(F("Enter device ID:"));
  readSerialLineBlocking(line, sizeof(line));
  if (strlen(line) >= DEVICE_ID_MAXLEN) line[DEVICE_ID_MAXLEN - 1] = '\0';
  strlcpy(device_id, line, sizeof(device_id));

  debugPrintln(F("Enter sensor offset (cm):"));
  readSerialLineBlocking(line, sizeof(line));
  sensor_offset_cm = atof(line);
  if (sensor_offset_cm < 0.0f) sensor_offset_cm = 0.0f;

  debugPrintln(F("Select sensor mode: 0=TRIG/ECHO, 1=UART SR04M-2 (default 0):"));
  readSerialLineBlocking(line, sizeof(line));
  if (!isBlank(line)) {
    uint8_t parsed = SENSOR_MODE_TRIG_ECHO;
    if (parseSensorMode(line, &parsed)) {
      sensor_mode = parsed;
    }
  }

  debugPrintln(F("Enter tank height (cm) (distance from water surface at full to bottom):"));
  readSerialLineBlocking(line, sizeof(line));
  tank_height_cm = atof(line);
  if (tank_height_cm <= 0.0f) tank_height_cm = 100.0f;

  debugPrintln(F("Enter measurement interval (seconds):"));
  readSerialLineBlocking(line, sizeof(line));
  interval_seconds = (uint32_t)strtoul(line, nullptr, 10);
  if (interval_seconds < 5) interval_seconds = 5;

  debugPrintln(F("Enter MQTT connect attempts (default 10):"));
  readSerialLineBlocking(line, sizeof(line));
  mqtt_connect_attempts = atoi(line);
  if (mqtt_connect_attempts < 1) mqtt_connect_attempts = 10;

  bool prod = production_mode;
  if (promptYesNoKeepCurrent(F("Enable production mode? (y/n):"), production_mode, &prod)) {
    production_mode = prod;
  }

  debugPrintln(F("Enter log level: 0=ERROR, 1=INFO, 2=DEBUG (default 2):"));
  readSerialLineBlocking(line, sizeof(line));
  if (!isBlank(line)) {
    int lv = atoi(line);
    if (lv < LOG_ERROR) lv = LOG_ERROR;
    if (lv > LOG_DEBUG) lv = LOG_DEBUG;
    stored_log_level = (uint8_t)lv;
  } else {
    stored_log_level = LOG_DEBUG;
  }
  updateLogLevel();

  enable_deep_sleep = true;

  writeConfigToFS();
  infoPrintln(F("Config saved. Rebooting..."));
  delay(1000);
  ESP.restart();
}

void promptForConfigEdit() {
  char line[SERIAL_LINE_MAX];

  debugPrintln(F("\nEdit settings. Leave input blank to keep the current value."));
  while (Serial.available()) Serial.read();

  debugPrint(F("WiFi SSID [")); Serial.print(wifi_ssid); Serial.println(F("]:"));
  readSerialLineBlocking(line, sizeof(line));
  if (!isBlank(line)) strlcpy(wifi_ssid, line, sizeof(wifi_ssid));

  debugPrintln(F("WiFi password "));
  readSerialLineBlocking(line, sizeof(line));
  if (!isBlank(line)) strlcpy(wifi_password, line, sizeof(wifi_password));

  debugPrint(F("MQTT server [")); Serial.print(mqtt_server); Serial.println(F("]:"));
  readSerialLineBlocking(line, sizeof(line));
  if (!isBlank(line)) strlcpy(mqtt_server, line, sizeof(mqtt_server));

  debugPrint(F("MQTT port [")); Serial.print(mqtt_port); Serial.println(F("]:"));
  readSerialLineBlocking(line, sizeof(line));
  if (!isBlank(line)) {
    int newPort = atoi(line);
    if (newPort > 0) mqtt_port = newPort;
  }

  debugPrint(F("MQTT connect attempts [")); Serial.print(mqtt_connect_attempts); Serial.println(F("]:"));
  readSerialLineBlocking(line, sizeof(line));
  if (!isBlank(line)) {
    int newAttempts = atoi(line);
    if (newAttempts > 0) mqtt_connect_attempts = newAttempts;
  }

  debugPrint(F("Device ID [")); Serial.print(device_id); Serial.println(F("]:"));
  readSerialLineBlocking(line, sizeof(line));
  if (!isBlank(line)) {
    if (strlen(line) >= DEVICE_ID_MAXLEN) line[DEVICE_ID_MAXLEN - 1] = '\0';
    strlcpy(device_id, line, sizeof(device_id));
  }

  debugPrint(F("Sensor offset (cm) [")); Serial.print(sensor_offset_cm); Serial.println(F("]:"));
  readSerialLineBlocking(line, sizeof(line));
  if (!isBlank(line)) {
    float newOffset = atof(line);
    if (newOffset >= 0.0f) sensor_offset_cm = newOffset;
  }

  debugPrint(F("Sensor mode ["));
  Serial.print(sensorModeName(sensor_mode));
  Serial.println(F("] (0=TRIG/ECHO, 1=UART SR04M-2):"));
  readSerialLineBlocking(line, sizeof(line));
  if (!isBlank(line)) {
    uint8_t parsed = SENSOR_MODE_TRIG_ECHO;
    if (parseSensorMode(line, &parsed)) {
      sensor_mode = parsed;
    } else {
      errorPrintln(F("Invalid sensor mode input. Keeping previous value."));
    }
  }

  debugPrint(F("Tank height (cm) [")); Serial.print(tank_height_cm); Serial.println(F("]:"));
  readSerialLineBlocking(line, sizeof(line));
  if (!isBlank(line)) {
    float th = atof(line);
    if (th > 0.0f) tank_height_cm = th;
  }

  debugPrint(F("Measurement interval (seconds) ["));
  Serial.print(interval_seconds);
  Serial.println(F("]:"));
  readSerialLineBlocking(line, sizeof(line));
  if (!isBlank(line)) {
    uint32_t newInterval = (uint32_t)strtoul(line, nullptr, 10);
    if (newInterval >= 5) interval_seconds = newInterval;
  }

  bool deepSleep = enable_deep_sleep;
  if (promptYesNoKeepCurrent(F("Deep sleep enabled (y/n):"), enable_deep_sleep, &deepSleep)) {
    enable_deep_sleep = deepSleep;
  }

  bool prod = production_mode;
  if (promptYesNoKeepCurrent(F("Production mode (y/n):"), production_mode, &prod)) {
    production_mode = prod;
  }

  bool ntp = ntp_enabled;
  if (promptYesNoKeepCurrent(F("NTP sync (y/n):"), ntp_enabled, &ntp)) {
    ntp_enabled = ntp;
  }

  debugPrint(F("NTP server [")); Serial.print(ntp_server); Serial.println(F("]:"));
  readSerialLineBlocking(line, sizeof(line));
  if (!isBlank(line)) strlcpy(ntp_server, line, sizeof(ntp_server));

  debugPrint(F("Log level ["));
  Serial.print(logLevelName(stored_log_level));
  Serial.println(F("] (0=ERROR, 1=INFO, 2=DEBUG):"));
  readSerialLineBlocking(line, sizeof(line));
  if (!isBlank(line)) {
    int lv = atoi(line);
    if (lv < LOG_ERROR) lv = LOG_ERROR;
    if (lv > LOG_DEBUG) lv = LOG_DEBUG;
    stored_log_level = (uint8_t)lv;
  }

  updateLogLevel();

  infoPrintln(F("Settings updated."));
  buildMqttTopics();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
  setupSensorInterface();
  saveConfigAndPublish();
}
