/*****************************************************************************************
* PROJECT NAME  : SMART GRID POWER DISTRIBUTION & MONITORING SYSTEM
* ---------------------------------------------------------------------------------------
* MODULE        : IoT Smart Energy Meter
* PROJECT MAKER : Rahul Kalwa
* INSTITUTE     : Choudhary Roopram Pvt. ITI, Rawatsar
* EVENT         : Arduino Physical AI Challenge India 2026
* TRACK         : Industrial, Smart Manufacturing & Sustainability
* CONTROLLER    : ESP32 Development Board
*
* PROJECT OVERVIEW:
* AI-Enabled Smart Energy Monitoring System designed for Real-Time
* Voltage, Current, Power, Energy and Frequency Measurement with
* Wireless IoT Monitoring, Web Dashboard Visualization, Blynk Cloud
* Integration and LCD-Based Local Display Interface.
*
* KEY FEATURES:
* • Real-Time Voltage Monitoring
* • Real-Time Current Monitoring
* • Power Consumption Analysis
* • Energy Usage Tracking (kWh)
* • Frequency Measurement
* • Wi-Fi Enabled Monitoring
* • Blynk IoT Cloud Dashboard
* • Live Web Dashboard with WebSocket
* • LCD Display Interface
* • Smart Grid Energy Management
*****************************************************************************************/



/**************** BLYNK SETTINGS ****************/
#define BLYNK_TEMPLATE_ID "YOUR_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "YOUR_TEMPLATE_NAME"
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_AUTH_TOKEN_HERE"


#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <PZEM004Tv30.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>


char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASSWORD";


#define RXD2 16
#define TXD2 17


PZEM004Tv30 pzem(Serial2, RXD2, TXD2);
LiquidCrystal_I2C lcd(0x27,16,2);


WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);


float voltage,current,power,energy,frequency;


BlynkTimer timer;
int screen=0;


float voltageCalibration = 1.144;   // ⭐ Voltage Calibration


String htmlPage(){


return R"====(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">


<title>RAHUL KALWA SMART ENERGY METER</title>


<style>
body{
margin:0;
font-family:Arial;
background:linear-gradient(135deg,#020617,#0f172a,#020617);
color:white;
text-align:center;
}
h1{
padding:15px;
font-size:22px;
}
.container{
display:grid;
grid-template-columns:repeat(auto-fit,minmax(140px,1fr));
gap:12px;
padding:15px;
}
.card{
background:#1e293b;
padding:18px;
border-radius:14px;
font-size:18px;
box-shadow:0 0 15px rgba(0,255,255,0.3);
transition:0.3s;
}
.card:hover{
transform:scale(1.05);
box-shadow:0 0 25px cyan;
}
</style>
</head>


<body>


<h1>⚡ RAHUL KALWA SMART ENERGY METER ⚡</h1>


<div class="container">
<div class="card">Voltage<br><span id="v">0</span> V</div>
<div class="card">Current<br><span id="i">0</span> A</div>
<div class="card">Power<br><span id="p">0</span> W</div>
<div class="card">Energy<br><span id="e">0</span> kWh</div>
<div class="card">Frequency<br><span id="f">0</span> Hz</div>
</div>


<script>


let ws = new WebSocket("ws://" + location.hostname + ":81");


ws.onmessage=function(event){


let data = JSON.parse(event.data);


document.getElementById("v").innerHTML=data.voltage;
document.getElementById("i").innerHTML=data.current;
document.getElementById("p").innerHTML=data.power;
document.getElementById("e").innerHTML=data.energy;
document.getElementById("f").innerHTML=data.frequency;


}


</script>


</body>
</html>
)====";


}


void sendData(){


voltage = pzem.voltage() * voltageCalibration;
current = pzem.current();
power = pzem.power();
energy = pzem.energy();
frequency = pzem.frequency();


String json="{";


json+="\"voltage\":"+String(voltage,1)+",";
json+="\"current\":"+String(current,2)+",";
json+="\"power\":"+String(power,1)+",";
json+="\"energy\":"+String(energy,3)+",";
json+="\"frequency\":"+String(frequency,1);


json+="}";


webSocket.broadcastTXT(json);


Blynk.virtualWrite(V0,voltage);
Blynk.virtualWrite(V1,current);
Blynk.virtualWrite(V2,power);
Blynk.virtualWrite(V3,energy);
Blynk.virtualWrite(V4,frequency);


}


void updateLCD(){


static unsigned long lcdTimer=0;


if(millis()-lcdTimer<2000) return;


lcdTimer=millis();


if(screen==0){


lcd.clear();


lcd.setCursor(0,0);
lcd.print("V:");
lcd.print(voltage,1);


lcd.setCursor(9,0);
lcd.print("I:");
lcd.print(current,2);


lcd.setCursor(0,1);
lcd.print("W:");
lcd.print(power,0);


lcd.setCursor(9,1);
lcd.print("Hz:");
lcd.print(frequency,1);


screen=1;


}


else{


lcd.clear();


lcd.setCursor(0,0);
lcd.print("Energy:");


lcd.setCursor(0,1);
lcd.print(energy,3);
lcd.print(" kWh");


screen=0;


}


}


void handleRoot(){
server.send(200,"text/html",htmlPage());
}


void setup(){


Serial.begin(115200);


Wire.begin(21,22);


lcd.init();
lcd.backlight();


lcd.setCursor(0,0);
lcd.print("RAHUL KALWA");
lcd.setCursor(0,1);
lcd.print("SMART ENERGY");


delay(3000);


WiFi.begin(ssid,pass);


lcd.clear();
lcd.print("Connecting WiFi");


while(WiFi.status()!=WL_CONNECTED){
delay(500);
}


lcd.clear();
lcd.print("WiFi Connected");
delay(1000);


lcd.clear();
lcd.setCursor(0,0);
lcd.print("IP Address");


lcd.setCursor(0,1);
lcd.print(WiFi.localIP());


delay(5000);


Serial2.begin(9600,SERIAL_8N1,RXD2,TXD2);


Blynk.begin(BLYNK_AUTH_TOKEN,ssid,pass);


server.on("/",handleRoot);
server.begin();


webSocket.begin();


timer.setInterval(2000L,sendData);


}


void loop(){


Blynk.run();
timer.run();


server.handleClient();
webSocket.loop();


updateLCD();


}




