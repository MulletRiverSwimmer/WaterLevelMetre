#include "app_state.h"

static char g_last_received_production_mode[32] = "absent";
static char g_last_received_control_origin[64] = "absent";
static bool g_last_received_has_production_mode = false;
static size_t g_last_received_payload_len = 0;

const char* mqttStateName(int state) {
  switch (state) {
    case MQTT_CONNECTION_TIMEOUT: return "TIMEOUT";
    case MQTT_CONNECTION_LOST: return "CONNECTION LOST";
    case MQTT_CONNECTED: return "CONNECTED";
    case MQTT_CONNECT_FAILED: return "CONNECT FAILED";
    case MQTT_DISCONNECTED: return "DISCONNECTED";
    case MQTT_CONNECT_BAD_PROTOCOL: return "BAD PROTOCOL";
    case MQTT_CONNECT_BAD_CLIENT_ID: return "BAD CLIENT ID";
    case MQTT_CONNECT_UNAVAILABLE: return "UNAVAILABLE";
    case MQTT_CONNECT_BAD_CREDENTIALS: return "BAD CREDENTIALS";
    case MQTT_CONNECT_UNAUTHORIZED: return "UNAUTHORIZED";
    default: return "UNKNOWN";
  }
}

void setupNtpTime() {
  if (!ntp_enabled) {
    infoPrintln(F("[TIME] NTP sync disabled"));
    return;
  }

  configTime(0, 0, ntp_server, "pool.ntp.org", "time.nist.gov");
  infoPrint(F("[TIME] Waiting for NTP sync to "));
  Serial.println(ntp_server);

  unsigned long start = millis();
  while (millis() - start < 15000) {
    if (time(nullptr) > 1600000000UL) break;
    delay(500);
  }

  time_t now = time(nullptr);
  if (now > 1600000000UL) {
    infoPrintln(F("[TIME] NTP synchronized"));
    struct tm* tmInfo = localtime(&now);
    if (tmInfo) {
      infoPrint(F("[TIME] Local time: "));
      Serial.println(asctime(tmInfo));
    }
  } else {
    errorPrintln(F("[TIME] NTP sync failed"));
  }
}

void setup_wifi() {
  delay(10);
  WiFi.mode(WIFI_STA);

  char clientId[33];
  buildClientId(clientId, sizeof(clientId));
  WiFi.hostname(clientId);

  WiFi.begin(wifi_ssid, wifi_password);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 30) {
    delay(500);
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    infoPrintln(F("[WIFI] Connected!"));
    printIpDetails();
    setupNtpTime();
  } else {
    errorPrintln(F("[WIFI] Failed to connect (timeout)."));
  }
}

void cacheMqttMessage(const char* topic, const char* payload) {
  File cache = LittleFS.open(CACHE_FILE, "a");
  if (!cache) return;

  cache.print(topic);
  cache.print('|');
  cache.println(payload);
  cache.close();
}

bool publishMqttMessage(const char* topic, const char* payload, bool retain, bool cacheOnFail) {
  if (!topic || !payload) {
    errorPrintln(F("[MQTT] Publish aborted: null topic or payload."));
    return false;
  }

  bool sent = client.publish(topic, payload, retain);

  debugPrint(F("[MQTT] Publishing to "));
  Serial.print(topic);
  Serial.print(F(" payload '"));
  Serial.print(payload);
  Serial.print(F("' -> "));
  Serial.println(sent ? F("OK") : F("FAIL"));

  if (!sent) {
    int state = client.state();
    errorPrint(F("[MQTT] Publish failed, rc="));
    Serial.print(state);
    Serial.print(F(" ("));
    Serial.print(mqttStateName(state));
    Serial.println(F(")"));

    if (cacheOnFail) {
      cacheMqttMessage(topic, payload);
      errorPrintln(F("[MQTT] Payload cached after publish failure."));
    }
  }

  return sent;
}

bool publishCachedLine(const char* line, const char* defaultTopic) {
  if (!line || line[0] == '\0') return true;

  char work[CACHE_LINE_MAX];
  char topic[96];
  char payload[160];

  strlcpy(work, line, sizeof(work));

  char* sep = strchr(work, '|');
  if (sep) {
    *sep = '\0';
    strlcpy(topic, work, sizeof(topic));
    strlcpy(payload, sep + 1, sizeof(payload));
  } else {
    strlcpy(topic, defaultTopic, sizeof(topic));
    strlcpy(payload, work, sizeof(payload));
  }

  return publishMqttMessage(topic, payload, true, false);
}

void buildMqttTopics() {
  snprintf(mqtt_topic, sizeof(mqtt_topic), "waterlevel/%s", device_id);
  snprintf(mqtt_config_set_topic, sizeof(mqtt_config_set_topic), "%s/config/set", mqtt_topic);
  snprintf(mqtt_config_get_topic, sizeof(mqtt_config_get_topic), "%s/config/get", mqtt_topic);
  snprintf(mqtt_config_ack_topic, sizeof(mqtt_config_ack_topic), "%s/config/ack", mqtt_topic);
}

void buildClientId(char* out, size_t outLen) {
  if (!out || outLen == 0) return;
  strlcpy(out, "WaterLevel_", outLen);
  strlcat(out, device_id, outLen);
}

void subscribeMqttTopics() {
  bool ok1 = client.subscribe(mqtt_config_set_topic);
  bool ok2 = client.subscribe(mqtt_config_get_topic);

  debugPrint(F("[MQTT] Subscribe "));
  Serial.print(mqtt_config_set_topic);
  Serial.print(F(" -> "));
  Serial.println(ok1 ? F("OK") : F("FAIL"));

  debugPrint(F("[MQTT] Subscribe "));
  Serial.print(mqtt_config_get_topic);
  Serial.print(F(" -> "));
  Serial.println(ok2 ? F("OK") : F("FAIL"));
}

void processIncomingMqtt(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (client.connected() && (millis() - start) < timeoutMs) {
    client.loop();
    delay(10);
  }
}

void publishConfigAck(bool success, const char* message) {
  char ackPayload[420];
  snprintf(ackPayload, sizeof(ackPayload),
           "{\"device_id\":\"%s\",\"success\":%s,\"message\":\"%s\",\"received_control_origin\":\"%s\",\"received_production_mode\":\"%s\",\"received_has_production_mode\":%s,\"received_payload_len\":%u,\"applied_production_mode\":%s,\"applied_enable_deep_sleep\":%s,\"applied_ntp_enabled\":%s}",
           device_id,
           success ? "true" : "false",
           message ? message : "",
           g_last_received_control_origin,
           g_last_received_production_mode,
           g_last_received_has_production_mode ? "true" : "false",
           (unsigned int)g_last_received_payload_len,
           production_mode ? "true" : "false",
           enable_deep_sleep ? "true" : "false",
           ntp_enabled ? "true" : "false");
  publishMqttMessage(mqtt_config_ack_topic, ackPayload, true, false);
}

bool applyRemoteConfigJson(const char* json, char* resultMsg, size_t resultMsgLen) {
  if (!json || !resultMsg || resultMsgLen == 0) return false;

  infoPrint(F("[CONFIG] Parsing JSON: "));
  Serial.println(json);
  g_last_received_payload_len = strlen(json);
  
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    snprintf(resultMsg, resultMsgLen, "invalid JSON");
    return false;
  }

  const char* origin = doc["control_origin"] | "";
  strlcpy(g_last_received_control_origin, origin, sizeof(g_last_received_control_origin));
  if (strcmp(origin, "nodered-dashboard") != 0) {
    snprintf(resultMsg, resultMsgLen, "ignored: unauthorized origin");
    return false;
  }

  bool changed = false;

  if (doc["production_mode"].isNull()) {
    g_last_received_has_production_mode = false;
    strlcpy(g_last_received_production_mode, "absent", sizeof(g_last_received_production_mode));
  } else {
    g_last_received_has_production_mode = true;
    String rawProd;
    serializeJson(doc["production_mode"], rawProd);
    strlcpy(g_last_received_production_mode, rawProd.c_str(), sizeof(g_last_received_production_mode));
  }

  auto parseBoolField = [](JsonVariantConst field, bool* out) -> bool {
    if (!out || field.isNull()) return false;

    if (field.is<bool>()) {
      *out = field.as<bool>();
      return true;
    }

    if (field.is<int>() || field.is<long>() || field.is<unsigned int>() || field.is<unsigned long>()) {
      long n = field.as<long>();
      *out = (n != 0);
      return true;
    }

    if (field.is<const char*>()) {
      const char* s = field.as<const char*>();
      if (!s) return false;

      if (strcasecmp(s, "true") == 0 || strcmp(s, "1") == 0 || strcasecmp(s, "on") == 0 || strcasecmp(s, "yes") == 0) {
        *out = true;
        return true;
      }

      if (strcasecmp(s, "false") == 0 || strcmp(s, "0") == 0 || strcasecmp(s, "off") == 0 || strcasecmp(s, "no") == 0) {
        *out = false;
        return true;
      }
    }

    return false;
  };

  if (doc["interval_seconds"].is<unsigned long>()) {
    uint32_t v = doc["interval_seconds"].as<uint32_t>();
    if (v >= 5) {
      interval_seconds = v;
      changed = true;
    }
  }

  if (doc["sensor_offset_cm"].is<float>() || doc["sensor_offset_cm"].is<int>()) {
    float v = doc["sensor_offset_cm"].as<float>();
    if (v >= 0.0f) {
      sensor_offset_cm = v;
      changed = true;
    }
  }

  if (doc["tank_height_cm"].is<float>() || doc["tank_height_cm"].is<int>()) {
    float v = doc["tank_height_cm"].as<float>();
    if (v > 0.0f) {
      tank_height_cm = v;
      changed = true;
    }
  }

  bool parsedBool = false;
  if (parseBoolField(doc["enable_deep_sleep"], &parsedBool)) {
    enable_deep_sleep = parsedBool;
    changed = true;
  }

  if (parseBoolField(doc["production_mode"], &parsedBool)) {
    infoPrint(F("[CONFIG] production_mode: "));
    Serial.print(production_mode);
    Serial.print(F(" -> "));
    Serial.println(parsedBool);
    production_mode = parsedBool;
    changed = true;
  } else if (!doc["production_mode"].isNull()) {
    String raw;
    serializeJson(doc["production_mode"], raw);
    infoPrint(F("[CONFIG] production_mode parse skipped, raw value: "));
    Serial.println(raw);
  }

  if (doc["log_level"].is<int>() || doc["log_level"].is<uint8_t>()) {
    int lv = doc["log_level"].as<int>();
    if (lv < LOG_ERROR) lv = LOG_ERROR;
    if (lv > LOG_DEBUG) lv = LOG_DEBUG;
    stored_log_level = (uint8_t)lv;
    changed = true;
  }

  if (parseBoolField(doc["ntp_enabled"], &parsedBool)) {
    ntp_enabled = parsedBool;
    changed = true;
  }

  if (doc["ntp_server"].is<const char*>()) {
    const char* s = doc["ntp_server"];
    if (s && strlen(s) > 0) {
      strlcpy(ntp_server, s, sizeof(ntp_server));
      changed = true;
    }
  }

  if (doc["mqtt_connect_attempts"].is<int>()) {
    int v = doc["mqtt_connect_attempts"].as<int>();
    if (v > 0) {
      mqtt_connect_attempts = v;
      changed = true;
    }
  }

  if (doc["sensor_mode"].is<int>() || doc["sensor_mode"].is<uint8_t>()) {
    int m = doc["sensor_mode"].as<int>();
    uint8_t newMode = (m == SENSOR_MODE_UART) ? SENSOR_MODE_UART : SENSOR_MODE_TRIG_ECHO;
    if (sensor_mode != newMode) {
      sensor_mode = newMode;
      setupSensorInterface();
      changed = true;
    }
  } else if (doc["sensor_mode"].is<const char*>()) {
    uint8_t parsed = sensor_mode;
    if (parseSensorMode(doc["sensor_mode"], &parsed) && parsed != sensor_mode) {
      sensor_mode = parsed;
      setupSensorInterface();
      changed = true;
    }
  }

  updateLogLevel();

  if (!changed) {
    infoPrintln(F("[CONFIG] No valid changes detected"));
    snprintf(resultMsg, resultMsgLen, "no valid changes");
    return false;
  }

  infoPrintln(F("[CONFIG] Changes detected; returning success"));
  snprintf(resultMsg, resultMsgLen, "config updated");
  return true;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (!topic || !payload) return;

  char msg[SETTINGS_PAYLOAD_MAX];
  size_t copyLen = (length < sizeof(msg) - 1) ? length : (sizeof(msg) - 1);
  memcpy(msg, payload, copyLen);
  msg[copyLen] = '\0';
  trimInPlace(msg);

  debugPrint(F("[MQTT] Message arrived ["));
  Serial.print(topic);
  Serial.print(F("] "));
  Serial.println(msg);

  if (strcmp(topic, mqtt_config_get_topic) == 0) {
    publishSettingsToMqtt();
    infoPrintln(F("[MQTT] config/get handled; settings published"));
    return;
  }

  if (strcmp(topic, mqtt_config_set_topic) == 0) {
    infoPrintln(F("[MQTT] config/set message received"));
    if (msg[0] == '\0') {
      infoPrintln(F("[MQTT] Empty config/set payload received; ignoring."));
      return;
    }

    char result[160];
    bool ok = applyRemoteConfigJson(msg, result, sizeof(result));
    infoPrint(F("[MQTT] applyRemoteConfigJson() returned: "));
    Serial.println(ok);
    publishConfigAck(ok, result);

    if (ok) {
      infoPrintln(F("[MQTT] Calling saveConfigAndPublish()"));
      saveConfigAndPublish();
    }
    return;
  }
}

void publishSettingsToMqtt() {
  char settingsTopic[96];
  snprintf(settingsTopic, sizeof(settingsTopic), "%s/settings", mqtt_topic);

  JsonDocument doc;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["firmware_build_date"] = FIRMWARE_BUILD_DATE;
  doc["wifi_ssid"] = wifi_ssid;
  doc["mqtt_server"] = mqtt_server;
  doc["mqtt_port"] = mqtt_port;
  doc["mqtt_connect_attempts"] = mqtt_connect_attempts;
  doc["device_id"] = device_id;
  doc["interval_seconds"] = interval_seconds;
  doc["sensor_offset_cm"] = sensor_offset_cm;
  doc["tank_height_cm"] = tank_height_cm;
  doc["enable_deep_sleep"] = enable_deep_sleep;
  doc["production_mode"] = production_mode;
  doc["log_level"] = stored_log_level;
  doc["ntp_enabled"] = ntp_enabled;
  doc["ntp_server"] = ntp_server;
  doc["sensor_mode"] = sensor_mode;
  doc["sensor_mode_name"] = sensorModeName(sensor_mode);
  doc["depth_cm"] = last_depth_measured;

  size_t required = measureJson(doc);
  debugPrint(F("[MQTT] Settings JSON length estimate: "));
  Serial.println(required);

  if (required == 0 || required >= sizeof(settings_payload_buf)) {
    errorPrintln(F("[MQTT] Settings payload too large for global buffer; skipping publish."));
    return;
  }

  size_t len = serializeJson(doc, settings_payload_buf, sizeof(settings_payload_buf));
  if (len == 0 || len >= sizeof(settings_payload_buf)) {
    errorPrintln(F("[MQTT] ERROR: serializeJson failed or payload truncated."));
    return;
  }

  logMemory(F("[MQTT] Before settings publish"));

  bool sent = publishMqttMessage(settingsTopic, settings_payload_buf, true, false);

  if (!sent) {
    errorPrintln(F("[MQTT] Settings publish failed; settings are persisted in LittleFS."));
  } else {
    infoPrintln(F("[MQTT] Settings published successfully!"));
  }

  logMemory(F("[MQTT] After settings publish"));
}

void reconnect() {
  int retries = 0;
  char clientId[33];
  buildClientId(clientId, sizeof(clientId));

  infoPrintln(F("[MQTT] Starting reconnect sequence..."));
  debugPrint(F("[MQTT] Client ID: "));
  Serial.println(clientId);

  debugPrint(F("[MQTT] Broker host: "));
  Serial.print(mqtt_server);
  Serial.print(':');
  Serial.println(mqtt_port);

  if (WiFi.status() == WL_CONNECTED) {
    debugPrint(F("[MQTT] WiFi status: "));
    Serial.print(WiFi.status());
    Serial.println(F(" (WL_CONNECTED)"));
    debugPrint(F("[MQTT] Local IP: "));
    Serial.println(WiFi.localIP());
    debugPrint(F("[MQTT] Gateway: "));
    Serial.println(WiFi.gatewayIP());
  } else {
    debugPrint(F("[MQTT] WiFi status: "));
    Serial.print(WiFi.status());
    Serial.println(F(" (NOT CONNECTED)"));
  }

  IPAddress brokerIp;
  if (WiFi.hostByName(mqtt_server, brokerIp)) {
    debugPrint(F("[MQTT] Resolved broker IP: "));
    Serial.println(brokerIp);
  } else {
    errorPrintln(F("[MQTT] Failed to resolve broker hostname"));
  }

  client.setServer(mqtt_server, mqtt_port);

#if defined(PUBSUBCLIENT_VERSION_MAJOR)
  client.setBufferSize(MQTT_MAX_PACKET_SIZE);
#endif

  bool pingResult = Ping.ping(mqtt_server, 1);
  if (pingResult) {
    debugPrint(F("[MQTT] Ping to "));
    Serial.print(mqtt_server);
    Serial.print(F(" successful, avg RTT "));
    Serial.print(Ping.averageTime());
    Serial.println(F(" ms"));
  } else {
    debugPrint(F("[MQTT] Ping to "));
    Serial.print(mqtt_server);
    Serial.println(F(" failed"));
  }

  while (!client.connected() && retries < mqtt_connect_attempts) {
    infoPrint(F("[MQTT] Attempt "));
    Serial.print(retries + 1);
    Serial.print(F(" of "));
    Serial.print(mqtt_connect_attempts);
    Serial.print(F(" connecting to "));
    Serial.print(mqtt_server);
    Serial.print(':');
    Serial.print(mqtt_port);
    Serial.println(F("..."));

    logMemory(F("[MQTT] Before connect"));

    if (client.connect(clientId)) {
      infoPrintln(F("[MQTT] Connection established!"));
      subscribeMqttTopics();
      processIncomingMqtt(1000);
      publishSettingsToMqtt();
      break;
    } else {
      int state = client.state();
      errorPrint(F("[MQTT] Connection failed, rc="));
      Serial.print(state);
      Serial.print(F(" ("));
      Serial.print(mqttStateName(state));
      Serial.println(F(")"));
      delay(500);
    }

    retries++;
  }

  if (!client.connected()) {
    errorPrint(F("[MQTT] Unable to connect after "));
    Serial.print(mqtt_connect_attempts);
    Serial.println(F(" attempts."));
  }
}
