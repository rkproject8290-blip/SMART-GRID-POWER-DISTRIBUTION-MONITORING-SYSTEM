/*****************************************************************************************
* PROJECT NAME  : SMART GRID POWER DISTRIBUTION & PROTECTION SYSTEM
* ---------------------------------------------------------------------------------------
* PROJECT MAKER : Rahul Kalwa
* EVENT         : Arduino Physical AI Challenge India 2026
* TRACK         : Industrial, Smart Manufacturing & Sustainability
*
* PROJECT OVERVIEW:
* An Autonomous Edge-AI Enabled Smart Grid Solution designed for Real-Time Power 
* Monitoring, Multi-Phase Fault Detection, Fault Distance Estimation, and Automated 
* Circuit Protection. The system utilizes embedded machine learning principles 
* (Euclidean Distance Classifier) on an ESP8266 NodeMCU to process current signature 
* profiles locally without relying on external cloud or internet connectivity.
*
* KEY FEATURES:
* • Real-Time Pure Edge-AI Grid Analytics
* • Symmetrical & Asymmetrical Fault Classification
* • Localized Tower Grounding Fault Discrimination (Dynamic G Detector)
* • High-Speed Power Isoaltion & Relay Protection (<250ms)
* • Intelligent Smart Advisor Screen (Local Diagnostics)
* • Dual-Row Dynamic LCD Interface & Buzzer Alarm Alert System
* • Hardware-Triggered Resiliency System (Manual Hard-Reset)
*****************************************************************************************/
/***********************************************************
   11KV LINE FAULT DETECTION - PURE EDGE AI AUTONOMOUS
   ESP8266 NODEMCU HARDWARE LATCH CONFIGURATION
************************************************************/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

/**************** LCD CONFIGURATION ****************/
LiquidCrystal_I2C lcd(0x27, 16, 2);

/**************** HARDWARE PIN MAP ****************/
#define R_RELAY   D7
#define Y_RELAY   D6
#define B_RELAY   D5

#define BUZZER    D0
#define RESET_BTN D3
#define CURRENT_PIN A0

/**************** SYSTEM CONFIGURATION ****************/
static const float FAULT_CURRENT_THRESHOLD_A = 0.60f; 

static const unsigned long PHASE_SCAN_DELAY_MS = 250UL; 
static const unsigned long ADC_SAMPLE_DELAY_MS = 2UL;
static const int ADC_SAMPLES = 40;

static const unsigned long HEALTHY_SCAN_CYCLE_MS = 3000UL; 
static const unsigned long FAULT_FREEZE_MS = 15000UL;
static const unsigned long ADVISOR_TOGGLE_MS = 3000UL;

/**************** DYNAMIC EDGE AI TARGETS ****************/
// Line-to-Line & Triple Phase (जब 2 या 3 फेजेस आपस में शॉर्ट हों)
static const float PHASE_2KM = 1.30f; 
static const float PHASE_4KM = 0.85f; 

// Line-to-Ground (जब केवल 1 फेज एल्युमिनियम फॉयल/Earth के साथ फॉल्ट हो)
static const float EARTH_2KM = 1.10f; 
static const float EARTH_4KM = 0.70f; 

/**************** SYSTEM STATE MACHINE ****************/
enum SystemMode
{
  MODE_HEALTHY_SCAN,
  MODE_FAULT_FREEZE,
  MODE_FAULT_ADVISOR
};

SystemMode mode = MODE_HEALTHY_SCAN;

bool lastButtonState = HIGH;
bool advisorPage = false;

float Ir = 0.0f;
float Iy = 0.0f;
float Ib = 0.0f;

float faultCurrent = 0.0f;
String faultString = "NONE";
String predictedZone = "NONE";
String advisorLine1 = "";
String advisorLine2 = "";

float sensorVccOffset = 1.65f; 

unsigned long faultLatchedAt = 0UL;
unsigned long lastAdvisorToggleAt = 0UL;
unsigned long lastHealthyScanStartAt = 0UL;
unsigned long lastDisplayRefreshAt = 0UL;

/**************** FORWARD DECLARATIONS ****************/
void resetSystem();
float readPhase(int relayPin);
String classifyDistance(float currentA, bool isEarthFault);
void latchFault(const String &phaseNames, float maxCurrentA, bool isEarthFault);
void isolateAllRelays();
void showHealthyScreen();
void showFaultFreezeScreen();
void updateAdvisorMode();
void renderAdvisorPages();
void printPaddedLine(uint8_t row, const String &text);

/**************** SETUP ****************/
void setup()
{
  Serial.begin(115200);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  pinMode(R_RELAY, OUTPUT);
  pinMode(Y_RELAY, OUTPUT);
  pinMode(B_RELAY, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(RESET_BTN, INPUT_PULLUP);

  isolateAllRelays();
  digitalWrite(BUZZER, LOW);

  lcd.setCursor(0, 0);
  lcd.print("AI GRID SYSTEM");
  lcd.setCursor(0, 1);
  lcd.print("CALIBRATING...");

  float offsetSum = 0.0f;
  for (int i = 0; i < 200; i++) {
    offsetSum += (analogRead(CURRENT_PIN) * 3.3f) / 1023.0f;
    delay(5);
  }
  sensorVccOffset = offsetSum / 200.0f;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("11KV GRID SYS");
  lcd.setCursor(0, 1);
  lcd.print("EDGE AI ONLINE");
  delay(1500);

  lcd.clear();
  lastHealthyScanStartAt = millis();
}

/**************** MAIN LOOP ****************/
void loop()
{
  const bool buttonState = digitalRead(RESET_BTN);

  if (buttonState == LOW && lastButtonState == HIGH && mode != MODE_HEALTHY_SCAN)
  {
    resetSystem();
  }
  lastButtonState = buttonState;

  // 1. HEALTHY SCAN MODE
  if (mode == MODE_HEALTHY_SCAN)
  {
    const unsigned long cycleStart = millis();

    Ir = readPhase(R_RELAY);
    Iy = readPhase(Y_RELAY);
    Ib = readPhase(B_RELAY);

    int faultCount = 0;
    bool rFault = (Ir > FAULT_CURRENT_THRESHOLD_A);
    bool yFault = (Iy > FAULT_CURRENT_THRESHOLD_A);
    bool bFault = (Ib > FAULT_CURRENT_THRESHOLD_A);

    if (rFault) faultCount++;
    if (yFault) faultCount++;
    if (bFault) faultCount++;

    if (faultCount > 0)
    {
      String combinedFault = "";
      float maxCurrent = 0.0f;

      // प्रभावित फेजेस के नाम जोड़ना
      if (rFault) { combinedFault += "R"; if(Ir > maxCurrent) maxCurrent = Ir; }
      if (yFault) { combinedFault += "Y"; if(Iy > maxCurrent) maxCurrent = Iy; }
      if (bFault) { combinedFault += "B"; if(Ib > maxCurrent) maxCurrent = Ib; }

      bool isEarthFault = false;

      // 🧠 इंटेलिजेंट फ़िल्टर लॉजिक
      if (faultCount == 1) 
      {
        // 'G' केवल तभी जुड़ेगा जब केवल 1 अकेला फेज शॉर्ट होगा
        isEarthFault = true;
        combinedFault += "G"; // परिणाम: RG, YG, BG
      }

      latchFault(combinedFault, maxCurrent, isEarthFault);
      return;
    }

    showHealthyScreen();

    unsigned long elapsed = millis() - cycleStart;
    if (elapsed < HEALTHY_SCAN_CYCLE_MS)
    {
      delay(HEALTHY_SCAN_CYCLE_MS - elapsed);
    }
    return;
  }

  // 2. FAULT FREEZE MODE
  if (mode == MODE_FAULT_FREEZE)
  {
    showFaultFreezeScreen();

    if (millis() - faultLatchedAt >= FAULT_FREEZE_MS)
    {
      mode = MODE_FAULT_ADVISOR;
      lastAdvisorToggleAt = millis();
      advisorPage = false;
      lcd.clear();
    }
    return;
  }

  // 3. FAULT ADVISOR MODE
  if (mode == MODE_FAULT_ADVISOR)
  {
    updateAdvisorMode();
    return;
  }
}

/**************** CURRENT SAMPLING ENGINE ****************/
float readPhase(int relayPin)
{
  digitalWrite(relayPin, LOW); 
  delay(PHASE_SCAN_DELAY_MS);

  long sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++)
  {
    sum += analogRead(CURRENT_PIN);
    delay(ADC_SAMPLE_DELAY_MS);
  }

  digitalWrite(relayPin, HIGH); 

  float adc = sum / (float)ADC_SAMPLES;
  float voltage = (adc * 3.3f) / 1023.0f;
  float current = (voltage - sensorVccOffset) / 0.066f;

  float finalCurrent = (current < 0.0f) ? -current : current;
  
  if (finalCurrent < 0.10f) finalCurrent = 0.0f; 
  
  return finalCurrent;
}

/**************** EUCLIDEAN DISTANCE AI CLASSIFIER ****************/
String classifyDistance(float currentA, bool isEarthFault)
{
  float delta2km = 0.0f;
  float delta4km = 0.0f;

  if (isEarthFault)
  {
    delta2km = fabs(currentA - EARTH_2KM);
    delta4km = fabs(currentA - EARTH_4KM);
  }
  else
  {
    delta2km = fabs(currentA - PHASE_2KM);
    delta4km = fabs(currentA - PHASE_4KM);
  }

  if (delta2km <= delta4km)
  {
    return "2KM"; 
  }
  return "4KM";   
}

/**************** FAULT LATCH MECHANISM ****************/
void latchFault(const String &phaseNames, float maxCurrentA, bool isEarthFault)
{
  faultLatchedAt = millis();
  mode = MODE_FAULT_FREEZE;
  faultString = phaseNames; 
  faultCurrent = maxCurrentA;
  
  predictedZone = classifyDistance(faultCurrent, isEarthFault);

  isolateAllRelays(); 
  digitalWrite(BUZZER, HIGH);

  Serial.println(F("\n=========================================="));
  Serial.println(F("🚨 [EDGE AI SUBSTATION: DETECTED & CLASSIFIED]"));
  Serial.print(F("↳ LATCHED CODES  : ")); Serial.println(faultString);
  Serial.print(F("↳ PEAK CURRENT   : ")); Serial.print(faultCurrent, 2); Serial.println(F(" A"));
  Serial.print(F("↳ PREDICTED ZONE : ")); Serial.print(predictedZone); Serial.println(F(" KM"));
  Serial.println(F("=========================================="));
}

/**************** RELAY SAFETY ISOLATION ****************/
void isolateAllRelays()
{
  digitalWrite(R_RELAY, HIGH);
  digitalWrite(Y_RELAY, HIGH);
  digitalWrite(B_RELAY, HIGH);
}

/**************** HEALTHY DISPLAY RENDER ****************/
void showHealthyScreen()
{
  if (millis() - lastDisplayRefreshAt < 250UL)
  {
    return;
  }
  lastDisplayRefreshAt = millis();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("R:"); lcd.print(Ir, 1);
  lcd.print(" Y:"); lcd.print(Iy, 1);

  lcd.setCursor(0, 1);
  lcd.print("B:"); lcd.print(Ib, 1);
  lcd.print(" GRID:OK");
}

/**************** FAULT FREEZE SCREEN RENDER ****************/
void showFaultFreezeScreen()
{
  lcd.setCursor(0, 0);
  lcd.print("FAULT: "); lcd.print(faultString);
  lcd.print(" "); lcd.print(faultCurrent, 1); lcd.print("A ");

  lcd.setCursor(0, 1);
  lcd.print("AI DIST: "); lcd.print(predictedZone);
}

/**************** ADVISOR LOGIC ****************/
void updateAdvisorMode()
{
  if (millis() - lastAdvisorToggleAt >= ADVISOR_TOGGLE_MS)
  {
    lastAdvisorToggleAt = millis();
    advisorPage = !advisorPage;
    lcd.clear();
  }

  renderAdvisorPages();
}

void renderAdvisorPages()
{
  bool hasGround = faultString.endsWith("G");
  bool isTriplePhase = (faultString == "RYB");

  if (predictedZone == "2KM")
  {
    if (advisorPage)
    {
      if (hasGround)       advisorLine1 = "AI: Earth Fault";
      else if(isTriplePhase) advisorLine1 = "AI: Symmetrical SC"; 
      else                 advisorLine1 = "AI: Line-Line SC";
      
      advisorLine2 = "Near Substation";
    }
    else
    {
      advisorLine1 = "Check Crossarms";
      advisorLine2 = "Radius: Zone 2KM";
    }
  }
  else
  {
    if (advisorPage)
    {
      if (hasGround)       advisorLine1 = "AI: Earth Fault";
      else if(isTriplePhase) advisorLine1 = "AI: Heavy 3-Ph SC";
      else                 advisorLine1 = "AI: Line-Line SC";
      
      advisorLine2 = "Tree Touch/Sag";
    }
    else
    {
      advisorLine1 = "Check Clearance";
      advisorLine2 = "Radius: Zone 4KM";
    }
  }

  printPaddedLine(0, advisorLine1);
  printPaddedLine(1, advisorLine2);
}

/**************** SYSTEM RESET FUNCTION ****************/
void resetSystem()
{
  mode = MODE_HEALTHY_SCAN;
  faultString = "NONE";
  predictedZone = "NONE";
  advisorLine1 = "";
  advisorLine2 = "";
  faultCurrent = 0.0f;
  Ir = 0.0f;
  Iy = 0.0f;
  Ib = 0.0f;
  advisorPage = false;
  faultLatchedAt = 0UL;
  lastAdvisorToggleAt = 0UL;
  lastHealthyScanStartAt = millis();

  isolateAllRelays();
  digitalWrite(BUZZER, LOW);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SYSTEM RESETTING");
  lcd.setCursor(0, 1);
  lcd.print("AI ENGINE READY");
  delay(2000);
  lcd.clear();
}

/**************** UTILITY: PADDED PRINT ****************/
void printPaddedLine(uint8_t row, const String &text)
{
  lcd.setCursor(0, row);
  lcd.print(text.substring(0, 16));
  for (uint8_t i = text.length(); i < 16; i++)
  {
    lcd.print(' ');
  }
}
