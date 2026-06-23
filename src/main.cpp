#include "app_state.h"

bool debug_mode = true;
bool production_mode = false;
uint8_t stored_log_level = LOG_DEBUG;
LogLevel currentLogLevel = LOG_DEBUG;
bool enable_deep_sleep = false;
bool ntp_enabled = true;
uint8_t sensor_mode = SENSOR_MODE_TRIG_ECHO;

float running_avg_buffer[RUNNING_AVG_SAMPLES] = {0.0f};
uint8_t running_avg_index = 0;
uint8_t running_avg_count = 0;
float running_avg_value = 0.0f;

float last_depth_measured = 0.0f;
float sensor_offset_cm = 0.0f;
float last_battery_voltage = 0.0f;
float last_battery_percent = 0.0f;
float tank_height_cm = 100.0f;
float last_tank_percent = 0.0f;

char wifi_ssid[32] = "";
char wifi_password[32] = "";
char mqtt_server[64] = "";
int mqtt_port = 1883;
int mqtt_connect_attempts = 10;
char ntp_server[64] = "pool.ntp.org";

char device_id[DEVICE_ID_MAXLEN] = "";
uint32_t interval_seconds = 600;

char mqtt_topic[64] = "";
char mqtt_config_set_topic[96] = "";
char mqtt_config_get_topic[96] = "";
char mqtt_config_ack_topic[96] = "";
char settings_payload_buf[SETTINGS_PAYLOAD_MAX] = {0};

unsigned int sensor_readings[NUM_READINGS] = {0};
unsigned int sensor_sorted[NUM_READINGS] = {0};

WiFiClient espClient;
PubSubClient client(espClient);

uint64_t getSleepTime() {
  return (uint64_t)interval_seconds * 1000000ULL;
}

void showTimestamp() {
  time_t t = time(nullptr);
  struct tm* tmInfo = localtime(&t);
  char buf[64];

  if (tmInfo) {
    strftime(buf, sizeof(buf), "[%Y-%m-%d %H:%M:%S] ", tmInfo);
  } else {
    unsigned long seconds = millis() / 1000UL;
    snprintf(buf, sizeof(buf), "[%s %s +%lu s] ", __DATE__, __TIME__, seconds);
  }

  Serial.print(buf);
}

const char* logLevelName(uint8_t level) {
  switch (level) {
    case LOG_ERROR: return "ERROR";
    case LOG_INFO: return "INFO";
    case LOG_DEBUG: return "DEBUG";
    default: return "UNKNOWN";
  }
}

const char* sensorModeName(uint8_t mode) {
  switch (mode) {
    case SENSOR_MODE_TRIG_ECHO: return "TRIG/ECHO";
    case SENSOR_MODE_UART: return "UART (SR04M-2)";
    default: return "UNKNOWN";
  }
}

bool parseSensorMode(const char* text, uint8_t* outMode) {
  if (!text || !outMode) return false;

  char normalized[24];
  strlcpy(normalized, text, sizeof(normalized));
  for (size_t i = 0; normalized[i] != '\0'; i++) {
    normalized[i] = (char)tolower((unsigned char)normalized[i]);
  }

  if (strcmp(normalized, "0") == 0 || strcmp(normalized, "trig") == 0 || strcmp(normalized, "trig_echo") == 0 || strcmp(normalized, "trig/echo") == 0) {
    *outMode = SENSOR_MODE_TRIG_ECHO;
    return true;
  }

  if (strcmp(normalized, "1") == 0 || strcmp(normalized, "uart") == 0 || strcmp(normalized, "sr04m2") == 0 || strcmp(normalized, "sr04m-2") == 0) {
    *outMode = SENSOR_MODE_UART;
    return true;
  }

  return false;
}

void updateLogLevel() {
  if (stored_log_level > LOG_DEBUG) {
    stored_log_level = LOG_DEBUG;
  }

  if (production_mode) {
    currentLogLevel = LOG_ERROR;
  } else {
    currentLogLevel = static_cast<LogLevel>(stored_log_level);
  }

  debug_mode = (!production_mode && currentLogLevel == LOG_DEBUG);
}

void cycleStoredLogLevel() {
  stored_log_level++;
  if (stored_log_level > LOG_DEBUG) {
    stored_log_level = LOG_ERROR;
  }
  updateLogLevel();
}

void logMemory(const __FlashStringHelper* tag) {
  if (currentLogLevel < LOG_DEBUG || production_mode) return;
  showTimestamp();
  Serial.print(tag);
  Serial.print(F(" FreeHeap="));
  Serial.print(ESP.getFreeHeap());
#ifdef ESP8266
  Serial.print(F(" Frag="));
  Serial.print(ESP.getHeapFragmentation());
  Serial.print(F("% ContStack="));
  Serial.println(ESP.getFreeContStack());
#else
  Serial.println();
#endif
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(30000);
  delay(200);

  Serial.println(F("\n[BOOT] Water Level Meter starting up..."));

#ifdef ESP32
  Serial1.begin(9600);
  Serial1.setTimeout(200);
  debugPrintln(F("[BOOT] Serial1 started for solar controller (ESP32)"));
#endif

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  if (!LittleFS.begin()) {
    errorPrintln(F("[ERROR] LittleFS mount failed"));
  } else {
    infoPrintln(F("[BOOT] LittleFS mounted successfully"));
  }

  if (!readConfigFromFS()) {
    production_mode = false;
    stored_log_level = LOG_DEBUG;
    updateLogLevel();
    Serial.println(F("[BOOT] Config missing or invalid; entering configuration mode..."));
    promptForFSConfig();
  }

  updateLogLevel();

  last_battery_voltage = readBatteryVoltage();

  debugPrintln(F("[BOOT] Loaded configuration:"));
  printConfig();
  logMemory(F("[BOOT] After config"));

  setup_wifi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
#if defined(PUBSUBCLIENT_VERSION_MAJOR)
  client.setBufferSize(MQTT_MAX_PACKET_SIZE);
#endif

  buildMqttTopics();

  infoPrint(F("[MQTT] Broker: "));
  Serial.print(mqtt_server);
  Serial.print(':');
  Serial.println(mqtt_port);

  infoPrint(F("[MQTT] Topic: "));
  Serial.println(mqtt_topic);

  reconnect();

  setupSensorInterface();
  delay(300);

  if (debug_mode) {
    showMenuWithTimeout(5000);
  }

  infoPrintln(F("[BOOT] Setup complete\n"));
  logMemory(F("[BOOT] End setup"));
}

void loop() {
  if (!production_mode) {
    handleSerialMenu();
  }

  if (client.connected()) {
    client.loop();
  }

  logMemory(F("[LOOP] Start"));

  digitalWrite(LED_PIN, LOW);

  char payload[160] = {0};
  measureWaterLevel(payload, sizeof(payload));

  digitalWrite(LED_PIN, HIGH);

  if (!client.connected()) {
    errorPrintln(F("[MQTT] Client not connected, attempting reconnect..."));
    reconnect();
  }

  bool allSent = true;
  bool cacheExisted = false;

  char measurementTopic[96];
  snprintf(measurementTopic, sizeof(measurementTopic), "%s/data", mqtt_topic);

  if (client.connected()) {
    File cache = LittleFS.open(CACHE_FILE, "r");
    if (cache) {
      cacheExisted = true;
      debugPrintln(F("[MQTT] Flushing cached payloads..."));

      char line[CACHE_LINE_MAX];
      while (cache.available()) {
        size_t n = cache.readBytesUntil('\n', line, sizeof(line) - 1);
        line[n] = '\0';
        trimInPlace(line);

        if (line[0] != '\0') {
          bool sent = publishCachedLine(line, measurementTopic);
          if (!sent) {
            allSent = false;
            break;
          }
        }
      }
      cache.close();
    }

    bool sent = publishMqttMessage(measurementTopic, payload, true, true);
    if (!sent) {
      allSent = false;
    }

    if (allSent) {
      debugPrintln(F("[MQTT] All payloads sent, clearing cache."));
      LittleFS.remove(CACHE_FILE);

      if (cacheExisted) {
        char secondPayload[160] = {0};
        debugPrintln(F("[MQTT] Cache backlog cleared, re-measuring and sending another payload."));

        digitalWrite(LED_PIN, LOW);
        measureWaterLevel(secondPayload, sizeof(secondPayload));
        digitalWrite(LED_PIN, HIGH);

        bool secondSent = publishMqttMessage(measurementTopic, secondPayload, true, true);

        if (!secondSent) {
          allSent = false;
        }
      }
    } else {
      errorPrintln(F("[MQTT] Publish failed; failed messages are cached."));
    }
  } else {
    errorPrintln(F("[MQTT] Still not connected, caching current payloads."));

    cacheMqttMessage(measurementTopic, payload);
  }

  logMemory(F("[LOOP] End"));

  if (enable_deep_sleep) {
    enterDeepSleep();
  } else {
    waitForNextMeasurement(interval_seconds * 1000UL);
  }
}
