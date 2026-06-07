/*****************************************************************************************
* PROJECT NAME  : SMART GRID POWER DISTRIBUTION & MONITORING SYSTEM
* ---------------------------------------------------------------------------------------
* MODULE        : Pure Edge-AI Smart Street Light Fault Isolation System
* PROJECT MAKER : Rahul Kalwa
* INSTITUTE     : Choudhary Roopram Pvt. ITI, Rawatsar
* EVENT         : Arduino Physical AI Challenge India 2026
* TRACK         : Industrial, Smart Manufacturing & Sustainability
* CONTROLLER    : ESP8266 NodeMCU (Pure Embedded Edge-AI Mode - No Display/No Serial)
*
* PROJECT OVERVIEW:
* An industrial-grade, fully independent Edge-AI protection controller. This code runs
* completely headless (without LCD or Serial dependencies) to simulate a real-world
* hardened substation environment. Fault isolation, multi-phase latching, and thermal 
* protection are processed locally on-chip with zero communication overhead.
*****************************************************************************************/

#include <ESP8266WiFi.h>
#include <espnow.h>

// ================= HARDWARE PIN MAP =================
#define R_GALI_RELAY D1
#define Y_GALI_RELAY D2
#define B_GALI_RELAY D0

#define R_PHASE_SENSE D5
#define Y_PHASE_SENSE D6
#define B_PHASE_SENSE D7

#define SYSTEM_RESET_SWITCH D3  // हार्डवेयर रीसेट (GND से टच करने पर)

// ================= FAULT DATA STRUCTURE =================
typedef struct struct_fault {
  char phase[6];
  int distance;
  bool isFaulty;
  bool isOverheating; 
} struct_fault;

struct_fault incomingFault;

// सिस्टम स्टेट्स और लैच वेरिएबल्स
bool rFaultLatched = false;
bool yFaultLatched = false;
bool bFaultLatched = false;
bool thermalFaultLatched = false; 

// ================= ESP-NOW LOCAL RECEIVE CALLBACK =================
void onDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
  if (len == sizeof(incomingFault)) {
    memcpy(&incomingFault, incomingData, sizeof(incomingFault));

    // ---- 1. वायरलेस फेज़ फॉल्ट पार्सिंग ----
    if (incomingFault.isFaulty) {
      String faultString = String(incomingFault.phase);

      if (faultString.indexOf("R") >= 0) rFaultLatched = true;
      if (faultString.indexOf("Y") >= 0) yFaultLatched = true;
      if (faultString.indexOf("B") >= 0) bFaultLatched = true;
    }

    // ---- 2. वायरलेस ट्रांसफार्मर थर्मल चेकिंग ----
    if (incomingFault.isOverheating) {
      thermalFaultLatched = true;
    }
  }
}

// ================= LOCAL HARDWARE SYSTEM RESET =================
void handleHardwareReset() {
  // जब D3 पिन को ग्राउंड किया जाए या फ्लैश बटन दबाया जाए
  if (digitalRead(SYSTEM_RESET_SWITCH) == LOW) {
    delay(50); // डिबाउंस डिले
    if (digitalRead(SYSTEM_RESET_SWITCH) == LOW) {
      
      rFaultLatched = false;
      yFaultLatched = false;
      bFaultLatched = false;
      thermalFaultLatched = false; 
      
      // सभी रिले को वापस नॉर्मल (INPUT/High) स्टेट में लाएं ताकि लाइट्स जल सकें
      pinMode(R_GALI_RELAY, INPUT);
      pinMode(Y_GALI_RELAY, INPUT);
      pinMode(B_GALI_RELAY, INPUT);
      
      delay(500); // दोबारा ट्रिगर होने से रोकने के लिए होल्ड
    }
  }
}

// ================= CORE AI PROTECTION LOGIC =================
void processGridLogic() {
  // 1. स्थानीय सेंसर (Local Sensors) से फेज़ फॉल्ट का रीयल-टाइम डिटेक्शन
  if (!rFaultLatched && digitalRead(R_PHASE_SENSE) == LOW) rFaultLatched = true;
  if (!yFaultLatched && digitalRead(Y_PHASE_SENSE) == LOW) yFaultLatched = true;
  if (!bFaultLatched && digitalRead(B_PHASE_SENSE) == LOW) bFaultLatched = true;

  // 2. ऑन-चिप डिसीजन इंजन: फॉल्ट होने पर रिले को तुरंत आइसोलेट (Tripping) करना
  // (जब लैच true होगा, तब रिले पिन OUTPUT मोड में जाकर LOW हो जाएगी और लाइट बुझ जाएगी)
  if (rFaultLatched) { 
    pinMode(R_GALI_RELAY, OUTPUT); 
    digitalWrite(R_GALI_RELAY, LOW); 
  }
  if (yFaultLatched) { 
    pinMode(Y_GALI_RELAY, OUTPUT); 
    digitalWrite(Y_GALI_RELAY, LOW); 
  }
  if (bFaultLatched) { 
    pinMode(B_GALI_RELAY, OUTPUT); 
    digitalWrite(B_GALI_RELAY, LOW); 
  }
}

// ================= SETUP =================
void setup() {
  // बिना सीरियल मॉनिटर के काम करने के लिए Serial.begin() को पूरी तरह हटा दिया गया है
  
  // रिले पिन्स को डिफ़ॉल्ट रूप से हाई/इनपुट (नॉर्मल ऑन) रखें
  pinMode(R_GALI_RELAY, INPUT);
  pinMode(Y_GALI_RELAY, INPUT);
  pinMode(B_GALI_RELAY, INPUT);

  // इंटरनल पुल-अप एक्टिवेशन (सेंसर्स के लिए)
  pinMode(R_PHASE_SENSE, INPUT_PULLUP);
  pinMode(Y_PHASE_SENSE, INPUT_PULLUP);
  pinMode(B_PHASE_SENSE, INPUT_PULLUP);
  pinMode(SYSTEM_RESET_SWITCH, INPUT_PULLUP); 

  // वाई-फाई को लोकल स्टेशन मोड में रखें (बिना इंटरनेट/राउटर के)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // स्थानीय वायरलेस नेटवर्क (ESP-NOW) की शुरुआत
  if (esp_now_init() == 0) {
    esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
    esp_now_register_recv_cb(onDataRecv);
  }
}

// ================= MAIN LOOP =================
void loop() {
  // 1. हमेशा फिजिकल रीसेट स्विच की निगरानी करें
  handleHardwareReset();

  // 2. बिना किसी टाइमर डिले के बिजली की रफ्तार (Real-time) से फॉल्ट चेक और एक्शन करें
  processGridLogic();
}
