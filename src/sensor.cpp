#include "app_state.h"

#ifdef ESP8266
#include <SoftwareSerial.h>
static SoftwareSerial sensorUart(SENSOR_UART_RX_PIN, SENSOR_UART_TX_PIN);
#endif

struct DistanceStats {
  unsigned int distance;
  unsigned int median;
  int validCount;
};

void setupSensorInterface() {
  if (sensor_mode == SENSOR_MODE_UART) {
#ifdef ESP8266
    sensorUart.begin(SENSOR_UART_BAUD);
    sensorUart.setTimeout(120);
    infoPrint(F("[SENSOR] Interface mode: "));
    Serial.println(F("UART SR04M-2"));
#else
    errorPrintln(F("[SENSOR] UART mode is not implemented for this board. Falling back to TRIG/ECHO."));
    sensor_mode = SENSOR_MODE_TRIG_ECHO;
#endif
  }

  if (sensor_mode == SENSOR_MODE_TRIG_ECHO) {
    pinMode(SENSOR_TRIG_PIN, OUTPUT);
    pinMode(SENSOR_ECHO_PIN, INPUT);
    digitalWrite(SENSOR_TRIG_PIN, LOW);
    infoPrint(F("[SENSOR] Interface mode: "));
    Serial.println(F("TRIG/ECHO"));
  }
}

static bool readDistanceTrigEcho(unsigned int* outDistanceCm) {
  if (!outDistanceCm) return false;
  *outDistanceCm = 0;

  digitalWrite(SENSOR_TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(SENSOR_TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(SENSOR_TRIG_PIN, LOW);

  unsigned long duration = pulseIn(SENSOR_ECHO_PIN, HIGH, 30000);
  if (duration == 0) return false;

  *outDistanceCm = (unsigned int)(duration / 58UL);
  return (*outDistanceCm > 0);
}

static bool readDistanceUart(unsigned int* outDistanceCm) {
  if (!outDistanceCm) return false;
  *outDistanceCm = 0;

#ifdef ESP8266
  while (sensorUart.available() > 0) {
    sensorUart.read();
  }

  sensorUart.write((uint8_t)0x55);

  uint8_t frame[4] = {0, 0, 0, 0};
  uint8_t count = 0;
  uint32_t start = millis();

  while ((millis() - start) < 150) {
    while (sensorUart.available() > 0) {
      uint8_t b = (uint8_t)sensorUart.read();
      if (count < 4) {
        frame[count++] = b;
      } else {
        frame[0] = frame[1];
        frame[1] = frame[2];
        frame[2] = frame[3];
        frame[3] = b;
      }

      if (count == 4 && frame[0] == 0xFF) {
        uint8_t checksum = (uint8_t)((frame[0] + frame[1] + frame[2]) & 0xFF);
        if (checksum == frame[3]) {
          uint16_t distanceMm = ((uint16_t)frame[1] << 8) | frame[2];
          float distanceCm = (float)distanceMm / 10.0f;
          if (distanceCm >= 2.0f && distanceCm <= 1200.0f) {
            *outDistanceCm = (unsigned int)(distanceCm + 0.5f);
            return true;
          }
        }
      }
    }
    delay(2);
  }
#endif

  return false;
}

static bool readSingleDistance(unsigned int* outDistanceCm) {
  if (sensor_mode == SENSOR_MODE_UART) {
    return readDistanceUart(outDistanceCm);
  }
  return readDistanceTrigEcho(outDistanceCm);
}

static DistanceStats takeFilteredDistance(bool logSamples) {
  unsigned int* readings = sensor_readings;
  unsigned int* sorted = sensor_sorted;

  for (int i = 0; i < NUM_READINGS; i++) {
    unsigned int d = 0;

    if (logSamples) {
      debugPrint(F("[SENSOR] Starting reading "));
      Serial.print(i + 1);
      Serial.print(F(" of "));
      Serial.println(NUM_READINGS);
    }

    bool ok = readSingleDistance(&d);

    if (!ok) {
      if (sensor_mode == SENSOR_MODE_UART) {
        errorPrintln(F("[SENSOR] timeout waiting for UART frame"));
      } else {
        errorPrintln(F("[SENSOR] timeout waiting for echo"));
      }
      d = 0;
    }

    readings[i] = d;

    if (logSamples) {
      debugPrint(F("[SENSOR] reading result: "));
      Serial.println(d);
    }

    delayWithSerial(READING_DELAY_MS);
  }

  memcpy(sorted, readings, sizeof(unsigned int) * NUM_READINGS);
  std::sort(sorted, sorted + NUM_READINGS);

  unsigned int median = sorted[NUM_READINGS / 2];
  unsigned int sum = 0;
  int validCount = 0;

  for (int i = 0; i < NUM_READINGS; i++) {
    if (readings[i] > 0 && abs((int)readings[i] - (int)median) <= OUTLIER_THRESHOLD_CM) {
      sum += readings[i];
      validCount++;
    }
  }

  DistanceStats stats;
  stats.distance = (validCount > 0) ? (sum / (unsigned int)validCount) : 0U;
  stats.median = median;
  stats.validCount = validCount;
  return stats;
}

float readBatteryVoltage() {
  float adc_ref = ADC_REFERENCE_VOLTAGE;

#ifdef ESP32
  Serial1.setTimeout(200);
  if (Serial1.available() > 0) {
    char line[32];
    size_t n = Serial1.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = '\0';
    trimInPlace(line);
    float ref = atof(line);
    if (ref > 2.0f && ref < 6.0f) {
      adc_ref = ref;
      debugPrint(F("[BATTERY] Using solar controller reference voltage: "));
      Serial.println(adc_ref);
    }
  }
#endif

#ifdef ESP32
  const float ADC_MAX = 4095.0f;
#else
  const float ADC_MAX = 1023.0f;
#endif

  int rawValue = analogRead(BATTERY_VOLTAGE_PIN);
  float measured = rawValue * adc_ref / ADC_MAX;
  float batteryVoltage = measured * BATTERY_VOLTAGE_DIVIDER_RATIO;

  const float BATTERY_MIN_V = 3.0f;
  const float BATTERY_MAX_V = 4.2f;
  float pct = (batteryVoltage - BATTERY_MIN_V) / (BATTERY_MAX_V - BATTERY_MIN_V) * 100.0f;
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;

  last_battery_percent = pct;
  return batteryVoltage >= 0.0f ? batteryVoltage : 0.0f;
}

float takeRawDistance() {
  DistanceStats stats = takeFilteredDistance(false);
  return (float)stats.distance;
}

void calibrateTankAndOffset() {
  char line[SERIAL_LINE_MAX];

  Serial.println(F("Calibration: place tank at FULL (water at max level), then press Enter to measure."));
  while (Serial.available()) Serial.read();
  readSerialLineBlocking(line, sizeof(line));

  float fullDist = takeRawDistance();
  Serial.print(F("Measured distance to full surface: "));
  Serial.print(fullDist, 1);
  Serial.println(F(" cm"));

  Serial.println(F("Now point sensor to the BOTTOM of the tank (or empty tank bottom), then press Enter to measure."));
  readSerialLineBlocking(line, sizeof(line));

  float bottomDist = takeRawDistance();
  Serial.print(F("Measured distance to bottom: "));
  Serial.print(bottomDist, 1);
  Serial.println(F(" cm"));

  sensor_offset_cm = fullDist;
  float th = bottomDist - fullDist;

  if (th <= 0.0f) {
    errorPrintln(F("Warning: measured bottom is not below full surface. Tank height not updated."));
  } else {
    tank_height_cm = th;
    infoPrint(F("Computed tank height: "));
    Serial.print(tank_height_cm, 1);
    Serial.println(F(" cm"));
  }

  saveConfigAndPublish();
  infoPrintln(F("Calibration complete and saved to /config.json"));
}

void measureWaterLevel(char* payload, size_t len) {
  DistanceStats stats = takeFilteredDistance(true);
  unsigned int distance = stats.distance;
  float adjusted_distance = distance > sensor_offset_cm ? (distance - sensor_offset_cm) : 0.0f;

  float water_depth = 0.0f;
  if (tank_height_cm > 0.0f) {
    water_depth = tank_height_cm - adjusted_distance;
    if (water_depth < 0.0f) water_depth = 0.0f;
    if (water_depth > tank_height_cm) water_depth = tank_height_cm;
  } else {
    water_depth = adjusted_distance;
  }

  last_depth_measured = water_depth;

  running_avg_buffer[running_avg_index] = water_depth;
  running_avg_index = (running_avg_index + 1U) % RUNNING_AVG_SAMPLES;
  if (running_avg_count < RUNNING_AVG_SAMPLES) running_avg_count++;

  float runningSum = 0.0f;
  for (uint8_t ri = 0; ri < running_avg_count; ri++) {
    runningSum += running_avg_buffer[ri];
  }
  running_avg_value = running_avg_count > 0 ? (runningSum / running_avg_count) : 0.0f;

  last_battery_voltage = readBatteryVoltage();

  if (tank_height_cm > 0.0f) {
    float pct = (last_depth_measured / tank_height_cm) * 100.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    last_tank_percent = pct;
  } else {
    last_tank_percent = 0.0f;
  }

  writeConfigToFS();

  debugPrint(F("[SENSOR] mode: "));
  Serial.println(sensorModeName(sensor_mode));
  debugPrint(F("[SENSOR] median: "));
  Serial.println(stats.median);
  debugPrint(F("[SENSOR] valid count: "));
  Serial.println(stats.validCount);
  debugPrint(F("[SENSOR] averaged distance: "));
  Serial.println(distance);
  debugPrint(F("[SENSOR] adjusted distance: "));
  Serial.println(adjusted_distance);
  debugPrint(F("[SENSOR] last depth measured: "));
  Serial.print(last_depth_measured, 1);
  Serial.println(F(" cm"));
  debugPrint(F("[SENSOR] running average: "));
  Serial.print(running_avg_value, 1);
  Serial.println(F(" cm"));

  snprintf(payload, len,
           "{\"device_id\":\"%s\",\"distance_cm\":%.1f,\"battery_v\":%.2f,\"battery_pct\":%.0f,\"tank_pct\":%.0f}",
           device_id,
           (float)distance,
           last_battery_voltage,
           last_battery_percent,
           last_tank_percent);
}

void enterDeepSleep() {
  infoPrintln(F("[DEEP SLEEP] All payloads sent, going to deep sleep now."));
  infoPrint(F("[DEEP SLEEP] Sleeping for "));
  Serial.print(interval_seconds);
  Serial.println(F(" seconds."));
  Serial.flush();
  delay(200);
  ESP.deepSleep(getSleepTime());
}
