#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266Ping.h>
#include <PubSubClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define LED_PIN D4

#define DEVICE_ID_MAXLEN 26

#define CONFIG_FILE "/config.json"
#define CACHE_FILE  "/cache.txt"

#define SENSOR_TRIG_PIN D2
#define SENSOR_ECHO_PIN D1
#define SENSOR_UART_TX_PIN D2
#define SENSOR_UART_RX_PIN D1

#define BATTERY_VOLTAGE_PIN A0

#define RUNNING_AVG_SAMPLES 10
#define NUM_READINGS 10
#define READING_DELAY_MS 500
#define OUTLIER_THRESHOLD_CM 10

#define SETTINGS_PAYLOAD_MAX 1024
#define SERIAL_LINE_MAX      96
#define CACHE_LINE_MAX       256
#define RIGHT_COL_MAX        96

enum LogLevel : uint8_t {
  LOG_ERROR = 0,
  LOG_INFO = 1,
  LOG_DEBUG = 2
};

enum SensorMode : uint8_t {
  SENSOR_MODE_TRIG_ECHO = 0,
  SENSOR_MODE_UART = 1
};

constexpr float ADC_REFERENCE_VOLTAGE = 3.3f;
constexpr float BATTERY_VOLTAGE_DIVIDER_RATIO = 2.0f;
constexpr uint32_t SENSOR_UART_BAUD = 9600;

extern bool debug_mode;
extern bool production_mode;
extern uint8_t stored_log_level;
extern LogLevel currentLogLevel;
extern bool enable_deep_sleep;
extern bool ntp_enabled;

extern float running_avg_buffer[RUNNING_AVG_SAMPLES];
extern uint8_t running_avg_index;
extern uint8_t running_avg_count;
extern float running_avg_value;

extern float last_depth_measured;
extern float sensor_offset_cm;
extern float last_battery_voltage;
extern float last_battery_percent;
extern float tank_height_cm;
extern float last_tank_percent;
extern uint8_t sensor_mode;

extern char wifi_ssid[32];
extern char wifi_password[32];
extern char mqtt_server[64];
extern int mqtt_port;
extern int mqtt_connect_attempts;
extern char ntp_server[64];

extern char device_id[DEVICE_ID_MAXLEN];
extern uint32_t interval_seconds;

extern char mqtt_topic[64];
extern char mqtt_config_set_topic[96];
extern char mqtt_config_get_topic[96];
extern char mqtt_config_ack_topic[96];
extern char settings_payload_buf[SETTINGS_PAYLOAD_MAX];

extern unsigned int sensor_readings[NUM_READINGS];
extern unsigned int sensor_sorted[NUM_READINGS];

extern WiFiClient espClient;
extern PubSubClient client;

uint64_t getSleepTime();
void showTimestamp();
const char* logLevelName(uint8_t level);
const char* sensorModeName(uint8_t mode);
bool parseSensorMode(const char* text, uint8_t* outMode);
void updateLogLevel();
void cycleStoredLogLevel();

template <typename T>
void logPrint(LogLevel level, const T& msg) {
  if (level > currentLogLevel) return;
  showTimestamp();
  Serial.print(msg);
}

template <typename T>
void logPrintln(LogLevel level, const T& msg) {
  if (level > currentLogLevel) return;
  showTimestamp();
  Serial.println(msg);
}

template <typename T>
void errorPrint(const T& msg) {
  logPrint(LOG_ERROR, msg);
}

template <typename T>
void errorPrintln(const T& msg) {
  logPrintln(LOG_ERROR, msg);
}

template <typename T>
void infoPrint(const T& msg) {
  logPrint(LOG_INFO, msg);
}

template <typename T>
void infoPrintln(const T& msg) {
  logPrintln(LOG_INFO, msg);
}

template <typename T>
void debugPrint(const T& msg) {
  logPrint(LOG_DEBUG, msg);
}

template <typename T>
void debugPrintln(const T& msg) {
  logPrintln(LOG_DEBUG, msg);
}

void writeConfigToFS();
bool readConfigFromFS();
void promptForFSConfig();
void promptForConfigEdit();

void setup_wifi();
void setupNtpTime();
void reconnect();

float takeRawDistance();
void measureWaterLevel(char* payload, size_t len);
float readBatteryVoltage();
void setupSensorInterface();

void calibrateTankAndOffset();
void publishSettingsToMqtt();
void saveConfigAndPublish();

void showMenu();
void handleSerialMenu();
void printConfig();
void printMenuLine(const char* left, const char* right);
void printIpDetails();
const char* mqttStateName(int state);

bool waitForSerialInput(uint32_t timeoutMs);
void showMenuWithTimeout(uint32_t timeoutMs);
void waitForNextMeasurement(uint32_t timeoutMs);
void delayWithSerial(uint32_t ms);

void cacheMqttMessage(const char* topic, const char* payload);
bool publishCachedLine(const char* line, const char* defaultTopic);
bool publishMqttMessage(const char* topic, const char* payload, bool retain, bool cacheOnFail);

void buildMqttTopics();
void buildClientId(char* out, size_t outLen);
void subscribeMqttTopics();
void mqttCallback(char* topic, byte* payload, unsigned int length);
bool applyRemoteConfigJson(const char* json, char* resultMsg, size_t resultMsgLen);
void publishConfigAck(bool success, const char* message);
void processIncomingMqtt(uint32_t timeoutMs);

bool readSerialLineBlocking(char* out, size_t outLen);
void trimInPlace(char* s);
bool isBlank(const char* s);
void logMemory(const __FlashStringHelper* tag);
bool promptYesNoKeepCurrent(const __FlashStringHelper* prompt, bool currentValue, bool* outValue);

void enterDeepSleep();
