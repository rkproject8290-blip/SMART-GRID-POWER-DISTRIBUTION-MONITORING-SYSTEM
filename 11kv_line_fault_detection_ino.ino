/*****************************************************************************************
* PROJECT NAME  : SMART GRID POWER DISTRIBUTION & MONITORING SYSTEM
* ---------------------------------------------------------------------------------------
* PROJECT MAKER : Rahul Kalwa
* EVENT         : Arduino Physical AI Challenge India 2026
* TRACK         : Industrial, Smart Manufacturing & Sustainability
*
* PROJECT OVERVIEW:
* An AI-Enabled Smart Grid Solution designed for Real-Time Power Monitoring,
* Multi-Phase Fault Detection, Fault Distance Estimation, Automated Protection,
* Smart Transformer Monitoring, IoT-Based Remote Supervision and Intelligent
* Power Distribution Management using ESP8266 NodeMCU, Blynk Cloud Platform,
* Current Sensors, Relay Protection System and LCD Display Interface.
*
* KEY FEATURES:
* • Real-Time Grid Monitoring
* • Phase-to-Phase Fault Detection
* • Fault Distance Estimation
* • Smart Relay-Based Protection
* • IoT Remote Monitoring & Alerts
* • LCD Status Display
* • Buzzer Alarm System
* • Manual & Cloud-Based Reset Control
*****************************************************************************************/
/***********************************************************
   11KV LINE FAULT DETECTION + BLYNK
   ESP8266 NODEMCU
************************************************************/

/**************** BLYNK SETTINGS ****************/
#define BLYNK_TEMPLATE_ID "YOUR_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "YOUR_TEMPLATE_NAME"
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_AUTH_TOKEN_HERE"

/**************** LIBRARIES ****************/
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

/**************** WIFI ****************/
char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASSWORD";

/**************** LCD ****************/
LiquidCrystal_I2C lcd(0x27, 16, 2);

/**************** PINS ****************/
#define R_RELAY D7
#define Y_RELAY D6
#define B_RELAY D5

#define BUZZER    D0
#define RESET_BTN D3
#define CURRENT_PIN A0

/**************** VARIABLES ****************/
bool faultLatched = false;
bool lastButtonState = HIGH;

bool Rf = false;
bool Yf = false;
bool Bf = false;

float Ir = 0;
float Iy = 0;
float Ib = 0;

float faultCurrent = 0;
int faultDistance = 0;

String faultString = "";

/**************** RESET FROM BLYNK ****************/
BLYNK_WRITE(V6)
{
  int value = param.asInt();

  if (value == 1)
  {
    resetSystem();
  }
}

/**************** SETUP ****************/
void setup()
{
  Serial.begin(115200);

  /******** LCD START ********/
  lcd.begin();
  lcd.backlight();

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("11KV LINE");

  lcd.setCursor(0,1);
  lcd.print("FAULT SYSTEM");

  delay(2000);

  lcd.clear();

  /******** RELAYS ********/
  pinMode(R_RELAY, OUTPUT);
  pinMode(Y_RELAY, OUTPUT);
  pinMode(B_RELAY, OUTPUT);

  digitalWrite(R_RELAY, HIGH);
  digitalWrite(Y_RELAY, HIGH);
  digitalWrite(B_RELAY, HIGH);

  /******** BUZZER ********/
  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, LOW);

  /******** RESET BUTTON ********/
  pinMode(RESET_BTN, INPUT_PULLUP);

  /******** BLYNK CONNECT ********/
  lcd.setCursor(0,0);
  lcd.print("CONNECT WIFI");

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("BLYNK ONLINE");

  Serial.println("WiFi Connected");
  Serial.println("Blynk Connected");

  delay(2000);
  lcd.clear();
}

/**************** LOOP ****************/
void loop()
{
  Blynk.run();

  /******** RESET BUTTON ********/
  bool buttonState = digitalRead(RESET_BTN);

  if (buttonState == LOW && lastButtonState == HIGH)
  {
    if (faultLatched)
    {
      resetSystem();
    }
  }

  lastButtonState = buttonState;

  /******** FAULT ACTIVE ********/
  if (faultLatched)
  {
    showFault(faultString);

    Blynk.virtualWrite(V3, faultString);
    Blynk.virtualWrite(V4, faultDistance);
    Blynk.virtualWrite(V5, faultCurrent);

    return;
  }

  /******** READ CURRENTS ********/
  Ir = readPhase(R_RELAY);
  Iy = readPhase(Y_RELAY);
  Ib = readPhase(B_RELAY);

  /******** SEND TO BLYNK ********/
  Blynk.virtualWrite(V0, Ir);
  Blynk.virtualWrite(V1, Iy);
  Blynk.virtualWrite(V2, Ib);

  /******** CHECK FAULT ********/
  Rf = isFault(Ir);
  Yf = isFault(Iy);
  Bf = isFault(Ib);

  if (Rf || Yf || Bf)
  {
    faultLatched = true;

    digitalWrite(BUZZER, HIGH);

    faultString = "";

    if (Rf) faultString += "R";
    if (Yf) faultString += "Y";
    if (Bf) faultString += "B";

    if (Rf)
    {
      faultCurrent = Ir;
    }
    else if (Yf)
    {
      faultCurrent = Iy;
    }
    else if (Bf)
    {
      faultCurrent = Ib;
    }

    faultDistance = calculateDistance(faultCurrent);

    /******** SEND FAULT DATA ********/
    Blynk.virtualWrite(V3, faultString);
    Blynk.virtualWrite(V4, faultDistance);
    Blynk.virtualWrite(V5, faultCurrent);

    Serial.println("FAULT DETECTED");
    Serial.print("PHASE: ");
    Serial.println(faultString);
  }

  /******** NORMAL LCD DISPLAY ********/
  lcd.clear();

  lcd.setCursor(0,0);
  lcd.print("R:");
  lcd.print(Ir,1);

  lcd.print(" Y:");
  lcd.print(Iy,1);

  lcd.setCursor(0,1);
  lcd.print("B:");
  lcd.print(Ib,1);

  lcd.print(" OK");

  delay(1000);
}

/**************** READ CURRENT ****************/
float readPhase(int relayPin)
{
  digitalWrite(relayPin, LOW);

  delay(300);

  long sum = 0;

  for (int i = 0; i < 40; i++)
  {
    sum += analogRead(CURRENT_PIN);
    delay(2);
  }

  digitalWrite(relayPin, HIGH);

  float adc = sum / 40.0;

  float voltage = (adc * 3.3) / 1023.0;

  float current = (voltage - 1.65) / 0.066;

  if (current < 0)
  {
    current = -current;
  }

  return current;
}

/**************** FAULT RANGE ****************/
bool isFault(float I)
{
  return (
           (I >= 16.70 && I <= 17.99) ||
           (I >= 16.01 && I <= 16.69)
         );
}

/**************** DISTANCE ****************/
int calculateDistance(float I)
{
  if (I >= 16.70 && I <= 17.99)
  {
    return 2;
  }

  if (I >= 16.01 && I <= 16.69)
  {
    return 4;
  }

  return 0;
}

/**************** SHOW FAULT ****************/
void showFault(String fault)
{
  lcd.clear();

  lcd.setCursor(0,0);
  lcd.print("FAULT:");
  lcd.print(fault);

  lcd.setCursor(0,1);
  lcd.print("I:");
  lcd.print(faultCurrent,1);

  lcd.print(" D:");
  lcd.print(faultDistance);

  lcd.print("K");
}

/**************** RESET SYSTEM ****************/
void resetSystem()
{
  faultLatched = false;

  digitalWrite(BUZZER, LOW);

  lcd.clear();

  lcd.setCursor(0,0);
  lcd.print("SYSTEM RESET");

  Blynk.virtualWrite(V3, "NO FAULT");
  Blynk.virtualWrite(V4, 0);
  Blynk.virtualWrite(V5, 0);

  delay(2000);

  lcd.clear();
}