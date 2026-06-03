/*****************************************************************************************
* PROJECT NAME  : SMART GRID POWER DISTRIBUTION & MONITORING SYSTEM
* ---------------------------------------------------------------------------------------
* MODULE        : Smart Street Light Fault Detection & Protection System
* PROJECT MAKER : Rahul Kalwa
* INSTITUTE     : Choudhary Roopram Pvt. ITI, Rawatsar
* EVENT         : Arduino Physical AI Challenge India 2026
* TRACK         : Industrial, Smart Manufacturing & Sustainability
* CONTROLLER    : ESP8266 NodeMCU
*
* PROJECT OVERVIEW:
* AI-Enabled Smart Street Light Monitoring System designed for
* Real-Time Phase Fault Detection, Automatic Fault Isolation,
* Transformer Protection, Wireless ESP-NOW Communication and
* IoT-Based Remote Monitoring using Blynk Cloud Platform.
*
* KEY FEATURES:
* • Real-Time Street Light Fault Detection
* • R, Y & B Phase Monitoring
* • Automatic Fault Isolation & Protection
* • Multi-Phase Fault Identification
* • Transformer Overheating Protection
* • Transformer Cut-Off Detection
* • Cooling Fan Activation Logic
* • ESP-NOW Wireless Communication
* • Blynk IoT Cloud Monitoring
* • Remote System Reset Control
* • Independent Relay-Based Fault Management
* • Smart Grid Street Lighting Automation
*****************************************************************************************/


#define BLYNK_TEMPLATE_ID "YOUR_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "YOUR_TEMPLATE_NAME"
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_AUTH_TOKEN_HERE"

#include <ESP8266WiFi.h>
#include <espnow.h>
#include <BlynkSimpleEsp8266.h>

// वाई-फाई क्रेडेंशियल्स
char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASSWORD";

// ================= PINS =================
#define R_GALI_RELAY D1
#define Y_GALI_RELAY D2
#define B_GALI_RELAY D0

#define R_PHASE_SENSE D5
#define Y_PHASE_SENSE D6
#define B_PHASE_SENSE D7

#define TRANS_CUTOFF_SENSE D3 // D8 की जगह अब D3 पिन का इस्तेमाल किया गया है

// ================= FAULT DATA =================
typedef struct struct_fault {
  char phase[6];
  int distance;
  bool isFaulty;
  bool isOverheating; 
} struct_fault;

struct_fault incomingFault;

bool wirelessFaultActive = false;

// फॉल्ट को सेव (Latch) रखने के लिए वेरिएबल्स
bool rFaultLatched = false;
bool yFaultLatched = false;
bool bFaultLatched = false;
bool thermalFaultLatched = false; 
bool transCutoffLatched = false; // D3 के लिए लैच वेरिएबल

unsigned long previousMillis = 0;
const long interval = 1500;

// ================= ESP-NOW RECEIVE =================
void onDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {

  if (len == sizeof(incomingFault)) {

    memcpy(&incomingFault, incomingData, sizeof(incomingFault));

    // ---- 1. फेज़ फॉल्ट चेकिंग ----
    if (incomingFault.isFaulty) {
      wirelessFaultActive = true;
      String faultString = String(incomingFault.phase);

      Serial.print("FAULT_DATA:");
      Serial.println(faultString);

      Blynk.virtualWrite(V0, faultString + " FAULT");

      if (faultString.indexOf("R") >= 0) rFaultLatched = true;
      if (faultString.indexOf("Y") >= 0) yFaultLatched = true;
      if (faultString.indexOf("B") >= 0) bFaultLatched = true;

    } else {
      wirelessFaultActive = false;
      Serial.println("SYSTEM_STATUS:OK");
    }

    // ---- 2. ट्रांसफार्मर थर्मल चेकिंग (V5 - वायरलेस) ----
    // अगर लोकल D3 एक्टिव नहीं है, तभी वायरलेस ओवरहीटिंग डेटा को प्राथमिकता दें
    if (!transCutoffLatched) {
      if (incomingFault.isOverheating) {
        thermalFaultLatched = true;
        Blynk.virtualWrite(V5, "OVERHEATING PROTECTION ON");
        Serial.println("TRANSFORMER_STATUS: WIRELESS OVERHEATING");
      } else if (!thermalFaultLatched) { 
        Blynk.virtualWrite(V5, "TRANSFORMER: HEALTHY");
      }
    }
  }
}

// ================= BLYNK RESET BUTTON (V4) =================
BLYNK_WRITE(V4) {
  int buttonState = param.asInt();
  
  if (buttonState == 1) { 
    rFaultLatched = false;
    yFaultLatched = false;
    bFaultLatched = false;
    thermalFaultLatched = false; 
    transCutoffLatched = false; // D3 का लैच भी रीसेट करें
    wirelessFaultActive = false;
    
    // सभी रिले को वापस नॉर्मल (INPUT/High) स्टेट में लाएं
    pinMode(R_GALI_RELAY, INPUT);
    pinMode(Y_GALI_RELAY, INPUT);
    pinMode(B_GALI_RELAY, INPUT);
    
    // Blynk डैशबोर्ड को तुरंत अपडेट करें
    Blynk.virtualWrite(V1, "NORMAL");
    Blynk.virtualWrite(V2, "NORMAL");
    Blynk.virtualWrite(V3, "NORMAL");
    Blynk.virtualWrite(V0, "LINE OK");
    Blynk.virtualWrite(V5, "TRANSFORMER: HEALTHY"); 
    
    Serial.println("SYSTEM_RESET: ALL FAULTS CLEARED AND LINES RESTORED");
  }
}

// ================= LOGIC =================
void checkLogicAndSensors() {

  // 1. स्थानीय सेंसर (Local Sensors) से फेज़ फॉल्ट चेक करें
  if (!rFaultLatched && digitalRead(R_PHASE_SENSE) == LOW) {
    rFaultLatched = true;
    Serial.println("AI_CMD:FAULT_R_LOCAL");
  }
  
  if (!yFaultLatched && digitalRead(Y_PHASE_SENSE) == LOW) {
    yFaultLatched = true;
    Serial.println("AI_CMD:FAULT_Y_LOCAL");
  }
  
  if (!bFaultLatched && digitalRead(B_PHASE_SENSE) == LOW) {
    bFaultLatched = true;
    Serial.println("AI_CMD:FAULT_B_LOCAL");
  }

  // ---- नया लॉजिक: जब D3 पिन GND से जुड़े (LOW हो) ----
  if (!transCutoffLatched && digitalRead(TRANS_CUTOFF_SENSE) == LOW) {
    transCutoffLatched = true;
    Serial.println("AI_CMD:TRANSFORMER_CUT_OFF_LOCAL");
  }

  // 2. सेव्ड (Latched) फॉल्ट्स के अनुसार रिले और Blynk को कंट्रोल करें
  
  // R Phase स्वतंत्र कंट्रोल
  if (rFaultLatched) {
    pinMode(R_GALI_RELAY, OUTPUT);
    digitalWrite(R_GALI_RELAY, LOW); 
    Blynk.virtualWrite(V1, "FAULT");
  } else {
    pinMode(R_GALI_RELAY, INPUT);
    Blynk.virtualWrite(V1, "NORMAL");
  }

  // Y Phase स्वतंत्र कंट्रोल
  if (yFaultLatched) {
    pinMode(Y_GALI_RELAY, OUTPUT);
    digitalWrite(Y_GALI_RELAY, LOW); 
    Blynk.virtualWrite(V2, "FAULT");
  } else {
    pinMode(Y_GALI_RELAY, INPUT);
    Blynk.virtualWrite(V2, "NORMAL");
  }

  // B Phase स्वतंत्र कंट्रोल
  if (bFaultLatched) {
    pinMode(B_GALI_RELAY, OUTPUT);
    digitalWrite(B_GALI_RELAY, LOW); 
    Blynk.virtualWrite(V3, "FAULT");
  } else {
    pinMode(B_GALI_RELAY, INPUT);
    Blynk.virtualWrite(V3, "NORMAL");
  }

  // 3. मुख्य डिस्प्ले (V0) पर ओवरऑल फॉल्ट स्टेटस अपडेट करना
  if (!rFaultLatched && !yFaultLatched && !bFaultLatched) {
    Blynk.virtualWrite(V0, "LINE OK");
  }
  else if (rFaultLatched && !yFaultLatched && !bFaultLatched) {
    Blynk.virtualWrite(V0, "R PHASE FAULT");
  }
  else if (!rFaultLatched && yFaultLatched && !bFaultLatched) {
    Blynk.virtualWrite(V0, "Y PHASE FAULT");
  }
  else if (!rFaultLatched && !yFaultLatched && bFaultLatched) {
    Blynk.virtualWrite(V0, "B PHASE FAULT");
  }
  else {
    Blynk.virtualWrite(V0, "MULTI PHASE FAULT");
  }

  // 4. थर्मल और कट-ऑफ स्टेटस डिस्प्ले लॉजिक (V5)
  if (transCutoffLatched) {
    // जब D3 को GND से टच किया गया हो
    Blynk.virtualWrite(V5, "TRANSFORMER CUT OFF\nCOOLING FAN ON");
  } 
  else if (thermalFaultLatched) {
    // अगर केवल वायरलेस के ज़रिए ओवरहीटिंग सिग्नल आया हो
    Blynk.virtualWrite(V5, "OVERHEATING PROTECTION ON");
  }
}

// ================= SETUP =================
void setup() {

  Serial.begin(115200);

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  pinMode(R_GALI_RELAY, INPUT);
  pinMode(Y_GALI_RELAY, INPUT);
  pinMode(B_GALI_RELAY, INPUT);

  pinMode(R_PHASE_SENSE, INPUT_PULLUP);
  pinMode(Y_PHASE_SENSE, INPUT_PULLUP);
  pinMode(B_PHASE_SENSE, INPUT_PULLUP);
  
  // D3 पिन को इंटरनल पुल-अप के साथ सेट किया ताकि बिना टच किए यह हमेशा HIGH रहे
  pinMode(TRANS_CUTOFF_SENSE, INPUT_PULLUP); 

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW Init Failed");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  esp_now_register_recv_cb(onDataRecv);

  // शुरुआती स्थिति सेट करें
  Blynk.virtualWrite(V0, "LINE OK");
  Blynk.virtualWrite(V1, "NORMAL");
  Blynk.virtualWrite(V2, "NORMAL");
  Blynk.virtualWrite(V3, "NORMAL");
  Blynk.virtualWrite(V5, "TRANSFORMER: HEALTHY"); 
}

// ================= LOOP =================
void loop() {

  Blynk.run();

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    checkLogicAndSensors();
  }
}