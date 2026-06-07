/*****************************************************************************************
* PROJECT NAME  : SMART GRID POWER DISTRIBUTION & MONITORING SYSTEM
* ---------------------------------------------------------------------------------------
* MODULE        : Edge-AI Autonomous Smart Energy Meter & Protection Relay
* PROJECT MAKER : Rahul Kalwa
* INSTITUTE     : Choudhary Roopram Pvt. ITI, Rawatsar
* EVENT         : Arduino Physical AI Challenge India 2026
* TRACK         : Industrial, Smart Manufacturing & Sustainability
* CONTROLLER    : ESP32 Development Board
*****************************************************************************************/

#include <PZEM004Tv30.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <cmath>

/**************** HARDWARE PIN MAP ****************/
#define RXD2 16
#define TXD2 17
#define MAIN_SUPPLY_RELAY 18  

/**************** METROLOGY & LCD CONFIGURATION ****************/
PZEM004Tv30 pzem(Serial2, RXD2, TXD2);
LiquidCrystal_I2C lcd(0x27, 16, 2);

/**************** PROTECTION & AI CONFIGURATION ****************/
static const float VOLTAGE_MAX_LIMIT_V = 250.0f;  
static const float VOLTAGE_MIN_LIMIT_V = 180.0f;  
static const float SANCTIONED_LOAD_W   = 1500.0f; 

static const float TARGET_NORMAL_W   = 0.0f;
static const float TARGET_OVERLOAD_W = SANCTIONED_LOAD_W;

static const float CALIBRATION_VOLTAGE_FACTOR = 1.144f; 
static const unsigned long DISPLAY_SWITCH_MS  = 5000UL;  // हर पेज 5 सेकंड रुकेगा
static const unsigned long SAMPLING_INTERVAL_MS = 1500UL;

/**************** SYSTEM VARIABLES ****************/
float voltage = 0.0f;
float current = 0.0f;
float power = 0.0f;
float energy = 0.0f;
float frequency = 0.0f;

String gridLoadState = "NORMAL";
String gridVoltageState = "V: HEALTHY";
bool isPowerCutActive = false;

unsigned long lastSampleTime = 0UL;
unsigned long lastDisplaySwitchTime = 0UL;
int displayScreen = 0;

/**************** FORWARD DECLARATIONS ****************/
void executeEdgeAiAndProtection();
void renderLcdInterface();
void printPaddedLine(uint8_t row, const String &text);

/**************** SETUP (PROFESSIONAL BOOTING) ****************/
void setup() {
  Serial.begin(115200);
  
  pinMode(MAIN_SUPPLY_RELAY, OUTPUT);
  digitalWrite(MAIN_SUPPLY_RELAY, LOW); 
  
  Wire.begin(21, 22);
  
  lcd.init();
  lcd.backlight();
  lcd.clear();
  
  // 🌟 PART 1: जजों के लिए ग्रैंड वेलकम (2.5 सेकंड)
  lcd.setCursor(0, 0);
  lcd.print("SMART ENERGY AI ");
  lcd.setCursor(0, 1);
  lcd.print("WELCOME SIR/MAM ");
  delay(2500); 
  
  // 🌟 PART 2: मेकर और इंस्टीट्यूशन क्रेडिट्स (2.5 सेकंड)
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("BY: RAHUL KALWA ");
  lcd.setCursor(0, 1);
  lcd.print("ROOPRAM ITI     ");
  delay(2500); 
  
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  lcd.clear();
}

/**************** MAIN LOOP ****************/
void loop() {
  unsigned long currentTime = millis();
  
  if (currentTime - lastSampleTime >= SAMPLING_INTERVAL_MS) {
    lastSampleTime = currentTime;
    
    float rawV = pzem.voltage();
    if (!std::isnan(rawV)) {
      voltage = rawV * CALIBRATION_VOLTAGE_FACTOR;
      current = pzem.current();
      power = pzem.power();
      energy = pzem.energy();
      frequency = pzem.frequency();
      
      executeEdgeAiAndProtection();
    } else {
      Serial.println(F("[ERROR] Metrology Hardware Fault"));
    }
  }
  
  renderLcdInterface();
}

/**************** EDGE-AI & PROTECTION ENGINE ****************/
void executeEdgeAiAndProtection() {
  if (voltage > VOLTAGE_MAX_LIMIT_V) {
    digitalWrite(MAIN_SUPPLY_RELAY, HIGH); 
    gridVoltageState = "OVER-V: TRIPPED";
    isPowerCutActive = true;
  } 
  else if (voltage < VOLTAGE_MIN_LIMIT_V) {
    digitalWrite(MAIN_SUPPLY_RELAY, LOW);  
    gridVoltageState = "LOW VOLTAGE WARN";
    isPowerCutActive = false;
  } 
  else {
    digitalWrite(MAIN_SUPPLY_RELAY, LOW);  
    gridVoltageState = "V: HEALTHY";
    isPowerCutActive = false;
  }

  float distanceToNormal   = std::fabs(power - TARGET_NORMAL_W);
  float distanceToOverload = std::fabs(power - TARGET_OVERLOAD_W);
  
  if (power > SANCTIONED_LOAD_W) {
    gridLoadState = "OVERLOAD WARNING";
  } else if (distanceToOverload < distanceToNormal && power >= (TARGET_OVERLOAD_W * 0.8f)) {
    gridLoadState = "HIGH LOAD ALERT";
  } else {
    gridLoadState = "NORMAL LOAD";
  }
}

/**************** LOCAL DIAGNOSTIC INTERFACE ****************/
void renderLcdInterface() {
  unsigned long currentTime = millis();
  
  if (currentTime - lastDisplaySwitchTime >= DISPLAY_SWITCH_MS) {
    lastDisplaySwitchTime = currentTime;
    displayScreen = (displayScreen + 1) % 3;
    lcd.clear();
  }
  
  switch(displayScreen) {
    case 0: 
      if (isPowerCutActive) {
        lcd.setCursor(0, 0);
        lcd.print("HIGH V: 250V+   ");
        lcd.setCursor(0, 1);
        lcd.print("POWER ISOLATED! ");
      } else {
        lcd.setCursor(0, 0);
        lcd.print("V:"); lcd.print(voltage, 1); lcd.print("V");
        lcd.setCursor(9, 0);
        lcd.print("I:"); lcd.print(current, 2); lcd.print("A");
        
        lcd.setCursor(0, 1);
        lcd.print("P:"); lcd.print(power, 0); lcd.print("W");
        lcd.setCursor(9, 1);
        lcd.print("F:"); lcd.print(frequency, 1); lcd.print("Hz");
      }
      break;
      
    case 1: 
      lcd.setCursor(0, 0);
      lcd.print("TOTAL CONSUMP.  ");
      lcd.setCursor(0, 1);
      lcd.print(energy, 3); lcd.print(" kWh");
      break;
      
    case 2: 
      lcd.setCursor(0, 0);
      printPaddedLine(0, gridVoltageState);
      printPaddedLine(1, gridLoadState);
      break;
  }
}

/**************** UTILITY: PADDED PRINT ****************/
void printPaddedLine(uint8_t row, const String &text) {
  lcd.setCursor(0, row);
  lcd.print(text.substring(0, 16));
  for (uint8_t i = text.length(); i < 16; i++) {
    lcd.print(' ');
  }
}
