#include <Wire.h>
#include <U8g2lib.h>
#include <math.h>

// ============================================================
// AI Based Smart Grid Street Light Overload Protection System
// Target Board: Arduino UNO Q (STM32U585)
// Hardware:
//  - 3 x ACS712-30A current sensors on A0, A1, A2
//  - 3 x relay channels driven through BC547 transistors
//  - 0.96" SSD1306 OLED I2C at address 0x3C
//
// Relay logic:
//  - LOW  = Relay OFF
//  - HIGH = Relay ON
// ============================================================

// OLED
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// Pin mapping
const uint8_t SENSOR_PINS[3] = {A0, A1, A2};
const uint8_t RELAY_PINS[3] = {5, 6, 7};

// Timing
const unsigned long CALIBRATION_TIME_MS = 5000UL;
const unsigned long SENSOR_PERIOD_MS = 20UL;
const unsigned long DISPLAY_PERIOD_MS = 250UL;
const unsigned long SERIAL_PERIOD_MS = 1000UL;
const unsigned long RETRY_WAIT_MS = 8000UL;
const unsigned long PROBE_SETTLE_MS = 500UL;

// ACS712 and ADC
const float ACS712_SENSITIVITY_V_PER_A = 0.066f; // 30A version
const float ADC_REFERENCE_VOLTAGE = 3.3f;
const float ADC_COUNTS = 1023.0f; // analogReadResolution(10)

// Filtering
const uint8_t FILTER_WINDOW = 16;
const uint16_t RMS_SAMPLE_COUNT = 96;
const uint16_t OFFSET_SAMPLE_COUNT = 200;
const uint8_t CONSECUTIVE_TRIP_COUNT = 8;
const uint8_t CONSECUTIVE_RESTORE_COUNT = 10;

// Threshold logic
const float MIN_THRESHOLD_A = 0.03f;
const float NOISE_FLOOR_A = 0.05f;
const float RESTORE_MARGIN_A = 0.05f;
const float THRESHOLD_MULTIPLIER = 1.80f;

enum LineMode : uint8_t {
  LINE_OK = 0,
  LINE_CUT = 1,
  LINE_PROBE = 2
};

struct LineState {
  const char *name;
  uint8_t sensorPin;
  uint8_t relayPin;
  float currentOffsetV;
  float baselineCurrentA;
  float thresholdA;
  float liveCurrentA;
  float faultCurrentA;
  float filterBuffer[FILTER_WINDOW];
  uint8_t filterIndex;
  uint8_t filterCount;
  float filterSum;
  unsigned long lastSampleMs;
  unsigned long tripTimeMs;
  unsigned long probeStartMs;
  LineMode mode;
  uint8_t tripCount;
  uint8_t restoreCount;
  bool relayOn;
};

static LineState makeLine(const char *name, uint8_t sensorPin, uint8_t relayPin) {
  LineState line;
  line.name = name;
  line.sensorPin = sensorPin;
  line.relayPin = relayPin;
  line.currentOffsetV = 1.65f;
  line.baselineCurrentA = 0.0f;
  line.thresholdA = 0.0f;
  line.liveCurrentA = 0.0f;
  line.faultCurrentA = 0.0f;
  for (uint8_t i = 0; i < FILTER_WINDOW; i++) {
    line.filterBuffer[i] = 0.0f;
  }
  line.filterIndex = 0;
  line.filterCount = 0;
  line.filterSum = 0.0f;
  line.lastSampleMs = 0;
  line.tripTimeMs = 0;
  line.probeStartMs = 0;
  line.mode = LINE_OK;
  line.tripCount = 0;
  line.restoreCount = 0;
  line.relayOn = false;
  return line;
}

LineState lines[3] = {
  makeLine("G1", SENSOR_PINS[0], RELAY_PINS[0]),
  makeLine("G2", SENSOR_PINS[1], RELAY_PINS[1]),
  makeLine("G3", SENSOR_PINS[2], RELAY_PINS[2])
};

enum SystemMode : uint8_t {
  SYS_STARTUP = 0,
  SYS_CALIBRATION = 1,
  SYS_MONITORING = 2
};

SystemMode systemMode = SYS_STARTUP;
unsigned long calibrationStartMs = 0;
unsigned long lastDisplayMs = 0;
unsigned long lastSerialMs = 0;

// ------------------------------------------------------------
// Relay helpers
// ------------------------------------------------------------
static void relayOff(uint8_t index) {
  lines[index].relayOn = false;
  digitalWrite(lines[index].relayPin, LOW);
}

static void relayOn(uint8_t index) {
  lines[index].relayOn = true;
  digitalWrite(lines[index].relayPin, HIGH);
}

static void allRelaysOff() {
  for (uint8_t i = 0; i < 3; i++) {
    relayOff(i);
  }
}

// ------------------------------------------------------------
// Current measurement helpers
// ------------------------------------------------------------
static float readInstantCurrentA(LineState &line) {
  int raw = analogRead(line.sensorPin);
  float voltage = (raw * ADC_REFERENCE_VOLTAGE) / ADC_COUNTS;
  float centered = voltage - line.currentOffsetV;
  return centered / ACS712_SENSITIVITY_V_PER_A;
}

static float readRmsCurrentA(LineState &line) {
  float sumSquares = 0.0f;
  for (uint16_t i = 0; i < RMS_SAMPLE_COUNT; i++) {
    float sample = readInstantCurrentA(line);
    sumSquares += sample * sample;
  }

  float rms = sqrtf(sumSquares / (float)RMS_SAMPLE_COUNT);
  if (rms < NOISE_FLOOR_A) {
    return 0.0f;
  }
  return rms;
}

static void calibrateOffset(LineState &line) {
  float sumV = 0.0f;
  for (uint16_t i = 0; i < OFFSET_SAMPLE_COUNT; i++) {
    int raw = analogRead(line.sensorPin);
    sumV += (raw * ADC_REFERENCE_VOLTAGE) / ADC_COUNTS;
    delayMicroseconds(150);
  }
  line.currentOffsetV = sumV / (float)OFFSET_SAMPLE_COUNT;
}

static float movingAverage(LineState &line, float sample) {
  if (line.filterCount < FILTER_WINDOW) {
    line.filterBuffer[line.filterIndex] = sample;
    line.filterSum += sample;
    line.filterCount++;
  } else {
    line.filterSum -= line.filterBuffer[line.filterIndex];
    line.filterBuffer[line.filterIndex] = sample;
    line.filterSum += sample;
  }

  line.filterIndex = (line.filterIndex + 1) % FILTER_WINDOW;
  return line.filterSum / (float)line.filterCount;
}

static float calculateThreshold(const LineState &line) {
  float scaledThreshold = line.baselineCurrentA * 1.80f;
  float marginThreshold = line.baselineCurrentA + 0.08f;
  float threshold = (scaledThreshold > marginThreshold) ? scaledThreshold : marginThreshold;
  return threshold;
}

static void refreshThresholds() {
  for (uint8_t i = 0; i < 3; i++) {
    lines[i].thresholdA = calculateThreshold(lines[i]);
  }
}

static void learnBaseline(LineState &line, float currentA) {
  // Average baseline learned during the 5-second calibration window.
  line.baselineCurrentA = (line.baselineCurrentA * 0.985f) + (currentA * 0.015f);
}

// ------------------------------------------------------------
// OLED helpers
// ------------------------------------------------------------
static void drawCalibrationScreen(unsigned long now) {
  display.clearBuffer();
  display.setFont(u8g2_font_6x12_tf);
  display.setFontPosTop();
  display.drawStr(18, 10, "AI CALIBRATION");
  display.setFont(u8g2_font_5x8_tf);
  display.drawStr(10, 30, "Learning baseline current...");

  unsigned long elapsed = now - calibrationStartMs;
  if (elapsed > CALIBRATION_TIME_MS) {
    elapsed = CALIBRATION_TIME_MS;
  }

  uint8_t progress = (uint8_t)((elapsed * 100UL) / CALIBRATION_TIME_MS);
  char buf[24];
  snprintf(buf, sizeof(buf), "Progress: %u%%", progress);
  display.drawStr(10, 44, buf);
  display.drawFrame(10, 54, 108, 8);
  display.drawBox(10, 54, (uint8_t)((108UL * progress) / 100UL), 8);
  display.sendBuffer();
}

static void drawMonitoringScreen(bool faultVisible, uint8_t faultIndex) {
  display.clearBuffer();
  display.setFont(u8g2_font_6x12_tf);
  display.setFontPosTop();
  display.drawStr(22, 2, "AI SMART GRID");
  display.setFont(u8g2_font_5x8_tf);

  if (faultVisible) {
    display.drawStr(8, 18, "OVERLOAD DETECTED");
    display.drawStr(8, 28, lines[faultIndex].name);
    char buf[24];
    snprintf(buf, sizeof(buf), "Trip %.2fA", lines[faultIndex].faultCurrentA);
    display.drawStr(8, 40, buf);
  } else {
    char row[24];
    for (uint8_t i = 0; i < 3; i++) {
      const char *status = "OK";
      if (lines[i].mode == LINE_CUT) {
        status = "CUT";
      }
      snprintf(row, sizeof(row), "%s: %.2fA %s", lines[i].name, lines[i].liveCurrentA, status);
      display.drawStr(6, 18 + (i * 13), row);
    }
  }

  display.sendBuffer();
}

// ------------------------------------------------------------
// Serial helpers
// ------------------------------------------------------------
static void printLineTelemetry(uint8_t index) {
  LineState &line = lines[index];
  Serial.print(line.name);
  Serial.print(F(" Current="));
  Serial.print(line.liveCurrentA, 3);
  Serial.print(F("A Baseline="));
  Serial.print(line.baselineCurrentA, 3);
  Serial.print(F("A Threshold="));
  Serial.print(line.thresholdA, 3);
  Serial.print(F("A TripCurrent="));
  Serial.print(line.faultCurrentA, 3);
  Serial.print(F("A "));
  if (line.mode == LINE_OK) {
    Serial.print(F("OK"));
  } else if (line.mode == LINE_CUT) {
    Serial.print(F("CUT"));
  } else {
    Serial.print(F("PROBE"));
  }
  Serial.print(F(" Relay="));
  Serial.println(line.relayOn ? F("ON") : F("OFF"));
}

static void printRelayStates() {
  for (uint8_t i = 0; i < 3; i++) {
    Serial.print(F("Relay"));
    Serial.print(i + 1);
    Serial.print(F("="));
    Serial.println(lines[i].relayOn ? F("ON") : F("OFF"));
  }
}

static void printCalibrationSummary() {
  Serial.println(F("Calibration complete."));
  for (uint8_t i = 0; i < 3; i++) {
    Serial.print(lines[i].name);
    Serial.print(F(" Baseline="));
    Serial.print(lines[i].baselineCurrentA, 3);
    Serial.print(F("A Threshold="));
    Serial.println(lines[i].thresholdA, 3);
  }
}

// ------------------------------------------------------------
// Fault handling
// ------------------------------------------------------------
static void tripLine(uint8_t index) {
  LineState &line = lines[index];
  line.mode = LINE_CUT;
  line.faultCurrentA = line.liveCurrentA;
  line.tripCount = 0;
  line.restoreCount = 0;
  line.tripTimeMs = millis();
  line.probeStartMs = 0;
  relayOn(index);
}

static void processLine(uint8_t index, unsigned long now) {
  LineState &line = lines[index];

  if (now - line.lastSampleMs < SENSOR_PERIOD_MS) {
    return;
  }
  line.lastSampleMs = now;

  float rmsCurrent = readRmsCurrentA(line);
  float filteredCurrent = movingAverage(line, rmsCurrent);
  line.liveCurrentA = filteredCurrent;

  if (systemMode == SYS_CALIBRATION) {
    learnBaseline(line, filteredCurrent);
    return;
  }

  line.thresholdA = calculateThreshold(line);

  float restoreLevel = line.baselineCurrentA + RESTORE_MARGIN_A;

  switch (line.mode) {
    case LINE_OK: {
      if (line.liveCurrentA > line.thresholdA) {
        line.tripCount++;
      } else {
        line.tripCount = 0;
      }

      if (line.tripCount >= CONSECUTIVE_TRIP_COUNT) {
        line.tripCount = 0;
        tripLine(index);
        Serial.print(F("TRIP "));
        Serial.println(line.name);
      } else {
        line.restoreCount = 0;
      }
    } break;

    case LINE_CUT: {
      // Keep relay ON while cut. Do not use current from this state
      // to restore, because the load is disconnected and will read ~0A.
      relayOn(index);

      if (now - line.tripTimeMs >= RETRY_WAIT_MS) {
        // Start a probe: briefly reconnect the line so real current can be measured.
        relayOff(index);
        line.mode = LINE_PROBE;
        line.probeStartMs = now;
        line.restoreCount = 0;
      }
    } break;

    case LINE_PROBE: {
      // Wait for the line to settle after temporary reconnect.
      if (now - line.probeStartMs < PROBE_SETTLE_MS) {
        relayOff(index);
        break;
      }

      // Measure current with the line temporarily restored.
      if (line.liveCurrentA > line.thresholdA) {
        // Overload still present: trip again and restart 8s timer.
        tripLine(index);
        Serial.print(F("TRIP "));
        Serial.println(line.name);
      } else {
        // Load is now safe: restore the line and return to normal.
        line.mode = LINE_OK;
        line.tripCount = 0;
        line.restoreCount = 0;
        relayOff(index);
        Serial.print(F("RESTORE "));
        Serial.println(line.name);
      }
    } break;
  }
}

// ------------------------------------------------------------
// Setup and loop
// ------------------------------------------------------------
void setup() {
  // UNO Q compatible ADC resolution for stable current readings.
  analogReadResolution(10);

  // Force relays OFF immediately.
  pinMode(5, OUTPUT);
  pinMode(6, OUTPUT);
  pinMode(7, OUTPUT);
  digitalWrite(5, LOW);
  digitalWrite(6, LOW);
  digitalWrite(7, LOW);
  delay(100);

  // Serial for telemetry.
  Serial.begin(115200);
  delay(200);
  Serial.println(F("AI SMART GRID STARTUP"));
  Serial.println(F("All relays OFF at boot"));

  // Initialize relay states.
  allRelaysOff();

  // I2C OLED.
  Wire.begin();
  Wire.setClock(100000UL);
  display.setI2CAddress(0x3C << 1);
  display.begin();
  display.setPowerSave(0);
  display.setContrast(200);
  drawCalibrationScreen(0);

  // Offset calibration before current learning.
  for (uint8_t i = 0; i < 3; i++) {
    calibrateOffset(lines[i]);
    Serial.print(lines[i].name);
    Serial.print(F(" OffsetV="));
    Serial.println(lines[i].currentOffsetV, 4);
  }

  calibrationStartMs = millis();
  systemMode = SYS_CALIBRATION;
  Serial.println(F("AI CALIBRATION"));
}

void loop() {
  unsigned long now = millis();

  if (systemMode == SYS_CALIBRATION) {
    for (uint8_t i = 0; i < 3; i++) {
      processLine(i, now);
    }

    if (now - calibrationStartMs >= CALIBRATION_TIME_MS) {
      refreshThresholds();
      printCalibrationSummary();
      systemMode = SYS_MONITORING;
      allRelaysOff();
      Serial.println(F("System State: MONITORING"));
    }
  } else {
    for (uint8_t i = 0; i < 3; i++) {
      processLine(i, now);
    }
    refreshThresholds();
  }

  if (now - lastDisplayMs >= DISPLAY_PERIOD_MS) {
    lastDisplayMs = now;
    if (systemMode == SYS_CALIBRATION) {
      drawCalibrationScreen(now);
    } else {
      bool faultVisible = false;
      uint8_t faultIndex = 0;
      for (uint8_t i = 0; i < 3; i++) {
        if (lines[i].mode == LINE_CUT) {
          faultVisible = true;
          faultIndex = i;
          break;
        }
      }
      drawMonitoringScreen(faultVisible, faultIndex);
    }
  }

  if (now - lastSerialMs >= SERIAL_PERIOD_MS) {
    lastSerialMs = now;
    Serial.println(F("---- Telemetry ----"));
    Serial.print(F("System State: "));
    Serial.println(systemMode == SYS_CALIBRATION ? F("CALIBRATION") : F("MONITORING"));
    for (uint8_t i = 0; i < 3; i++) {
      printLineTelemetry(i);
    }
    printRelayStates();
  }
}
