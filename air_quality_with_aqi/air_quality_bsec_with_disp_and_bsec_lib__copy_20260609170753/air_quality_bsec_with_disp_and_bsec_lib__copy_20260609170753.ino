/* ============================================================
   Air Quality Monitor — ESP32
   Using BSEC library for proper IAQ index from BME680
   IAQ 0-50   = Excellent
   IAQ 51-100 = Good
   IAQ 101-150= Lightly polluted
   IAQ 151-200= Moderately polluted
   IAQ 201-250= Heavily polluted
   IAQ 251-350= Severely polluted
   IAQ 351+   = Extremely polluted
   ============================================================ */

#define BLYNK_TEMPLATE_ID   "TMPL39HRvkpe5"
#define BLYNK_TEMPLATE_NAME "Quickstart Template"
#define BLYNK_AUTH_TOKEN    "ZgxFiMwCYXHQp_YwDsfMvhSL-VhzX6wt"
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include "bsec.h"   // Bosch BSEC library (replaces Adafruit BME680)
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── Display ───────────────────────────────────────────────────────────────────
#define SCREEN_W  128
#define SCREEN_H   64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

// ── Button ────────────────────────────────────────────────────────────────────
#define BTN_PIN 4
int  currentScreen = 0;
bool lastBtnState  = HIGH;
unsigned long lastDebounce = 0;

// ── WiFi ──────────────────────────────────────────────────────────────────────
const char* WIFI_SSID_1 = "Prakyath pg 2 floor";
const char* WIFI_PASS_1 = "Kgs@12342";
const char* WIFI_SSID_2 = "Sup";
const char* WIFI_PASS_2 = "12345678";
const char* AP_SSID     = "AirMonitor";
const char* AP_PASS     = "airquality";

// ── Hardware ──────────────────────────────────────────────────────────────────
HardwareSerial mtp(2);
HardwareSerial pms(1);
Bsec bme;          // BSEC object (not Adafruit_BME680 anymore)
WiFiMulti wifiMulti;
WebServer server(80);
BlynkTimer timer;

// ── Sensor globals ────────────────────────────────────────────────────────────
int32_t  g_co2      = 0;
uint16_t g_pm1      = 0, g_pm25 = 0, g_pm10 = 0;
float    g_temp     = 0, g_hum  = 0, g_pres = 0;
float    g_iaq      = 0;          // IAQ index 0-500
uint8_t  g_iaqAcc   = 0;          // accuracy 0=unreliable 1=low 2=medium 3=high
float    g_co2Equiv = 0;          // BSEC CO2 equivalent (different from MTP80)
float    g_voc      = 0;          // breath VOC equivalent
int      g_pmAqi    = 0;
String   g_pmAqiLabel  = "---";
String   g_iaqLabel    = "---";
String   g_co2Status   = "warming up";
bool     g_pmsValid    = false;
bool     g_bsecOk      = false;

// ── IAQ label (Bosch scale) ───────────────────────────────────────────────────
String iaqLabel(float iaq) {
  if (iaq <= 50)  return "Excellent";
  if (iaq <= 100) return "Good";
  if (iaq <= 150) return "Lt Polluted";
  if (iaq <= 200) return "Mod Polluted";
  if (iaq <= 250) return "Hvy Polluted";
  if (iaq <= 350) return "Svr Polluted";
  return "Extreme";
}
String iaqColor(float iaq) {
  if (iaq <= 50)  return "#00e400";
  if (iaq <= 100) return "#92d050";
  if (iaq <= 150) return "#ffff00";
  if (iaq <= 200) return "#ff7e00";
  if (iaq <= 250) return "#ff0000";
  if (iaq <= 350) return "#8f3f97";
  return "#7e0023";
}

// ── PM AQI (US EPA) ───────────────────────────────────────────────────────────
int aqiFromPM25(float pm) {
  float b[][4] = {{0,12,0,50},{12.1,35.4,51,100},{35.5,55.4,101,150},
                  {55.5,150.4,151,200},{150.5,250.4,201,300},
                  {250.5,350.4,301,400},{350.5,500.4,401,500}};
  for (auto& r : b) if (pm<=r[1]) return (int)((r[3]-r[2])/(r[1]-r[0])*(pm-r[0])+r[2]);
  return 500;
}
int aqiFromPM10(float pm) {
  float b[][4] = {{0,54,0,50},{55,154,51,100},{155,254,101,150},
                  {255,354,151,200},{355,424,201,300},
                  {425,504,301,400},{505,604,401,500}};
  for (auto& r : b) if (pm<=r[1]) return (int)((r[3]-r[2])/(r[1]-r[0])*(pm-r[0])+r[2]);
  return 500;
}
String pmAqiLabel(int a) {
  if (a<=50)  return "Good";
  if (a<=100) return "Moderate";
  if (a<=150) return "USG";
  if (a<=200) return "Unhealthy";
  if (a<=300) return "Very Bad";
  return "Hazardous";
}

// ── MTP80-A CO2 ───────────────────────────────────────────────────────────────
const uint8_t CO2_CMD[] = {0x42,0x4D,0xA0,0x00,0x03,0x00,0x00,0x01,0x32};
void readCO2() {
  while (mtp.available()) mtp.read();
  mtp.write(CO2_CMD, 9);
  uint32_t t=millis();
  while (mtp.available()<14) { if (millis()-t>1000) { g_co2Status="no response"; return; } }
  uint8_t buf[14];
  for (int i=0;i<14;i++) buf[i]=mtp.read();
  if (buf[0]!=0x42||buf[1]!=0x4D) { g_co2Status="bad frame"; return; }
  if (buf[11]!=0x00) { g_co2Status="warming up"; return; }
  g_co2 = ((int32_t)buf[7]<<24)|((int32_t)buf[8]<<16)|((int32_t)buf[9]<<8)|(int32_t)buf[10];
  g_co2Status = "ok";
}

// ── PMS7003 ───────────────────────────────────────────────────────────────────
void readPMS() {
  while (pms.available()) pms.read();
  uint32_t t=millis(); uint8_t buf[32]; int idx=0;
  while (millis()-t<2000) {
    if (pms.available()) {
      uint8_t b=pms.read();
      if (idx==0&&b!=0x42) continue;
      if (idx==1&&b!=0x4D) { idx=0; continue; }
      buf[idx++]=b; if (idx==32) break;
    }
  }
  if (idx<32) { g_pmsValid=false; return; }
  uint16_t sum=0;
  for (int i=0;i<30;i++) sum+=buf[i];
  if (sum!=(((uint16_t)buf[30]<<8)|buf[31])) { g_pmsValid=false; return; }
  g_pm1  = ((uint16_t)buf[10]<<8)|buf[11];
  g_pm25 = ((uint16_t)buf[12]<<8)|buf[13];
  g_pm10 = ((uint16_t)buf[14]<<8)|buf[15];
  g_pmsValid   = true;
  g_pmAqi      = max(aqiFromPM25(g_pm25), aqiFromPM10(g_pm10));
  g_pmAqiLabel = pmAqiLabel(g_pmAqi);
}

// ── BSEC / BME680 ─────────────────────────────────────────────────────────────
void readBSEC() {
  if (!g_bsecOk) return;
  if (bme.run()) {
    g_temp     = bme.temperature;
    g_hum      = bme.humidity;
    g_pres     = bme.pressure / 100.0;
    g_iaq      = bme.iaq;
    g_iaqAcc   = bme.iaqAccuracy;
    g_co2Equiv = bme.co2Equivalent;
    g_voc      = bme.breathVocEquivalent;
    g_iaqLabel = iaqLabel(g_iaq);
  } else {
    checkBsecStatus();
  }
}

void checkBsecStatus() {
  if (bme.bsecStatus != BSEC_OK) {
    Serial.printf("BSEC error: %d\n", bme.bsecStatus);
  }
  if (bme.bme68xStatus != BME68X_OK) {
    Serial.printf("BME68x error: %d\n", bme.bme68xStatus);
  }
}

// ── OLED Screens ──────────────────────────────────────────────────────────────
void drawScreenIndicator(int s) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(98, 56);
  display.print("["); display.print(s+1); display.print("/3]");
}

// Screen 0: CO2 (real) + IAQ from BSEC
void drawScreen0() {
  display.clearDisplay();
  display.fillRect(0,0,128,12,SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(22,2); display.print("CO2 & Air IAQ");

  display.setTextColor(SSD1306_WHITE);

  // CO2
  display.setTextSize(1); display.setCursor(0,15); display.print("CO2:");
  if (g_co2Status=="ok") {
    display.setTextSize(2); display.setCursor(30,13);
    display.print(g_co2); display.setTextSize(1); display.print("ppm");
  } else {
    display.setCursor(30,15); display.print(g_co2Status);
  }

  display.drawFastHLine(0,30,128,SSD1306_WHITE);

  // IAQ
  display.setTextSize(1); display.setCursor(0,33); display.print("IAQ:");
  display.setTextSize(2); display.setCursor(30,31);
  display.print((int)g_iaq);
  display.setTextSize(1);
  // Accuracy indicator
  display.setCursor(70,33);
  if      (g_iaqAcc==0) display.print("(cal...)");
  else if (g_iaqAcc==1) display.print("(low)");
  else if (g_iaqAcc==2) display.print("(med)");
  else                  display.print("(high)");

  display.setCursor(0,48); display.print(g_iaqLabel);

  drawScreenIndicator(0);
  display.display();
}

// Screen 1: Particulates + PM AQI
void drawScreen1() {
  display.clearDisplay();
  display.fillRect(0,0,128,12,SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(16,2); display.print("Particulates");

  display.setTextColor(SSD1306_WHITE);
  if (!g_pmsValid) {
    display.setCursor(10,28); display.print("PMS7003 no data");
  } else {
    display.setCursor(0,15);  display.print("PM1.0:"); display.setCursor(50,15); display.print(g_pm1);  display.print(" ug/m3");
    display.setCursor(0,27);  display.print("PM2.5:"); display.setCursor(50,27); display.print(g_pm25); display.print(" ug/m3");
    display.setCursor(0,39);  display.print("PM10 :"); display.setCursor(50,39); display.print(g_pm10); display.print(" ug/m3");
    display.setCursor(0,51);  display.print("AQI:"); display.setCursor(28,51);
    display.print(g_pmAqi); display.print(" "); display.print(g_pmAqiLabel);
  }
  drawScreenIndicator(1);
  display.display();
}

// Screen 2: Climate
void drawScreen2() {
  display.clearDisplay();
  display.fillRect(0,0,128,12,SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(34,2); display.print("Climate");

  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,15); display.print("Temp :"); display.setCursor(50,15); display.print(g_temp,1); display.print(" C");
  display.setCursor(0,26); display.print("Humid:"); display.setCursor(50,26); display.print(g_hum,1);  display.print(" %");
  display.setCursor(0,37); display.print("Press:"); display.setCursor(50,37); display.print(g_pres,1); display.print(" hPa");
  display.setCursor(0,48); display.print("VOC  :"); display.setCursor(50,48); display.print(g_voc,2);  display.print(" ppm");

  drawScreenIndicator(2);
  display.display();
}

void updateDisplay() {
  switch(currentScreen) {
    case 0: drawScreen0(); break;
    case 1: drawScreen1(); break;
    case 2: drawScreen2(); break;
  }
}

// ── Button ────────────────────────────────────────────────────────────────────
void checkButton() {
  bool state = digitalRead(BTN_PIN);
  if (state==LOW && lastBtnState==HIGH && millis()-lastDebounce>200) {
    currentScreen = (currentScreen+1) % 3;
    updateDisplay();
    lastDebounce = millis();
  }
  lastBtnState = state;
}

// ── Read all + update display ─────────────────────────────────────────────────
void readAllSensors() {
  readCO2();
  readPMS();
  readBSEC();   // BSEC handles its own timing internally
  updateDisplay();
  Serial.printf("CO2:%dppm  IAQ:%.0f(%s) acc:%d\n", g_co2, g_iaq, g_iaqLabel.c_str(), g_iaqAcc);
  Serial.printf("PM2.5:%d PM10:%d AQI:%d\n", g_pm25, g_pm10, g_pmAqi);
  Serial.printf("Temp:%.1fC Hum:%.1f%% Pres:%.1fhPa VOC:%.2fppm\n", g_temp, g_hum, g_pres, g_voc);
  Serial.println("─────────────────────────────");
}

// ── Blynk (60s to save messages) ──────────────────────────────────────────────
// V0=CO2  V1=PM1  V2=PM2.5  V3=PM10  V4=Temp  V5=Hum  V6=Pres  V7=IAQ  V8=PM_AQI  V9=VOC
void sendToBlynk() {
  if (WiFi.status()!=WL_CONNECTED || !Blynk.connected()) return;
  if (g_co2Status=="ok") Blynk.virtualWrite(V0, g_co2);
  if (g_pmsValid) {
    Blynk.virtualWrite(V1, g_pm1);
    Blynk.virtualWrite(V2, g_pm25);
    Blynk.virtualWrite(V3, g_pm10);
    Blynk.virtualWrite(V8, g_pmAqi);
  }
  Blynk.virtualWrite(V4, g_temp);
  Blynk.virtualWrite(V5, g_hum);
  Blynk.virtualWrite(V6, g_pres);
  Blynk.virtualWrite(V7, g_iaq);   // IAQ replaces gas resistance
  Blynk.virtualWrite(V9, g_voc);
}

// ── Web dashboard ─────────────────────────────────────────────────────────────
void handleRoot() {
  String ic = iaqColor(g_iaq);
  String ac = g_pmAqi<=50?"#00e400":g_pmAqi<=100?"#ffff00":g_pmAqi<=150?"#ff7e00":g_pmAqi<=200?"#ff0000":"#8f3f97";
  String accStr = g_iaqAcc==0?"Calibrating...":g_iaqAcc==1?"Low accuracy":g_iaqAcc==2?"Medium accuracy":"High accuracy";

  String html = "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta http-equiv='refresh' content='10'><title>Air Quality Monitor</title>"
    "<style>*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:sans-serif;background:#1a1a2e;color:#eee;padding:16px}"
    "h1{text-align:center;margin-bottom:16px;font-size:1.3em;color:#00d4ff}"
    ".grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}"
    ".card{background:#16213e;border-radius:12px;padding:14px;text-align:center}"
    ".label{font-size:.75em;color:#aaa;margin-bottom:4px}"
    ".value{font-size:1.6em;font-weight:bold}.unit{font-size:.75em;color:#aaa}"
    ".wide{grid-column:1/-1;border:2px solid " + ic + "}"
    ".wide .value{color:" + ic + ";font-size:2.2em}"
    ".acc{font-size:.7em;color:#aaa;margin-top:4px}"
    ".footer{text-align:center;margin-top:14px;font-size:.7em;color:#555}</style></head><body>"
    "<h1>🌿 Air Quality Monitor</h1><div class='grid'>";

  html += "<div class='card wide'><div class='label'>IAQ — Indoor Air Quality (Bosch BSEC)</div>"
          "<div class='value'>" + String((int)g_iaq) + "</div>"
          "<div class='unit'>" + g_iaqLabel + "</div>"
          "<div class='acc'>" + accStr + "</div></div>";

  html += "<div class='card'><div class='label'>CO₂ (MTP80-A)</div><div class='value' style='color:#00d4ff'>" + String(g_co2) + "</div><div class='unit'>ppm</div></div>";
  html += "<div class='card'><div class='label'>VOC Equivalent</div><div class='value'>" + String(g_voc,2) + "</div><div class='unit'>ppm</div></div>";
  html += "<div class='card'><div class='label'>PM AQI</div><div class='value' style='color:" + ac + "'>" + String(g_pmAqi) + "</div><div class='unit'>" + g_pmAqiLabel + "</div></div>";
  html += "<div class='card'><div class='label'>PM2.5</div><div class='value'>" + String(g_pm25) + "</div><div class='unit'>µg/m³</div></div>";
  html += "<div class='card'><div class='label'>PM10</div><div class='value'>" + String(g_pm10) + "</div><div class='unit'>µg/m³</div></div>";
  html += "<div class='card'><div class='label'>PM1.0</div><div class='value'>" + String(g_pm1) + "</div><div class='unit'>µg/m³</div></div>";
  html += "<div class='card'><div class='label'>Temperature</div><div class='value'>" + String(g_temp,1) + "</div><div class='unit'>°C</div></div>";
  html += "<div class='card'><div class='label'>Humidity</div><div class='value'>" + String(g_hum,1) + "</div><div class='unit'>%</div></div>";
  html += "<div class='card'><div class='label'>Pressure</div><div class='value'>" + String(g_pres,1) + "</div><div class='unit'>hPa</div></div>";
  html += "</div><div class='footer'>Auto-refreshes every 10s | IP: " + WiFi.localIP().toString() + "</div></body></html>";
  server.send(200, "text/html", html);
}

void handleJSON() {
  String j = "{\"co2\":" + String(g_co2) + ",\"iaq\":" + String(g_iaq,1) +
             ",\"iaq_label\":\"" + g_iaqLabel + "\",\"iaq_accuracy\":" + String(g_iaqAcc) +
             ",\"voc\":" + String(g_voc,2) + ",\"co2_equiv\":" + String(g_co2Equiv,1) +
             ",\"pm1\":" + String(g_pm1) + ",\"pm25\":" + String(g_pm25) +
             ",\"pm10\":" + String(g_pm10) + ",\"pm_aqi\":" + String(g_pmAqi) +
             ",\"temp\":" + String(g_temp,2) + ",\"humidity\":" + String(g_hum,2) +
             ",\"pressure\":" + String(g_pres,2) + "}";
  server.send(200, "application/json", j);
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  mtp.begin(9600, SERIAL_8N1, 16, 17);
  pms.begin(9600, SERIAL_8N1, 25, 26);
  Wire.begin(21, 22);
  pinMode(BTN_PIN, INPUT_PULLUP);

  // OLED splash
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 not found!");
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1); display.setCursor(20,15); display.print("Air Quality");
    display.setTextSize(1); display.setCursor(30,28); display.print("Monitor");
    display.setTextSize(1); display.setCursor(12,45); display.print("BSEC Initialising");
    display.display();
  }

  // BSEC / BME680
  bme.begin(0x77, Wire);   // your BME680 is at 0x77
  if (bme.bsecStatus == BSEC_OK) {
    g_bsecOk = true;
    Serial.println("BSEC OK");

    // Tell BSEC what outputs we want
    bsec_virtual_sensor_t sensorList[] = {
      BSEC_OUTPUT_IAQ,
      BSEC_OUTPUT_STATIC_IAQ,
      BSEC_OUTPUT_CO2_EQUIVALENT,
      BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
      BSEC_OUTPUT_RAW_TEMPERATURE,
      BSEC_OUTPUT_RAW_PRESSURE,
      BSEC_OUTPUT_RAW_HUMIDITY,
      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
    };
    bme.updateSubscription(sensorList, 9, BSEC_SAMPLE_RATE_LP);
    // LP = low power mode, samples every 3 seconds — good for air quality
  } else {
    Serial.printf("BSEC init failed: %d\n", bme.bsecStatus);
  }

  // WiFi
  wifiMulti.addAP(WIFI_SSID_1, WIFI_PASS_1);
  wifiMulti.addAP(WIFI_SSID_2, WIFI_PASS_2);
  Serial.print("Connecting WiFi");
  int tries=0;
  while (wifiMulti.run()!=WL_CONNECTED && tries++<20) { delay(500); Serial.print("."); }
  if (WiFi.status()==WL_CONNECTED) {
    Serial.println("\nConnected: " + WiFi.SSID() + " → " + WiFi.localIP().toString());
    Blynk.config(BLYNK_AUTH_TOKEN);
    Blynk.connect(3000);
  } else {
    Serial.println("\nNo WiFi — AP only");
  }
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", handleRoot);
  server.on("/json", handleJSON);
  server.begin();

  timer.setInterval(3000L,  readAllSensors);  // every 3s (matches BSEC LP rate)
  timer.setInterval(60000L, sendToBlynk);

  delay(1500);
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  checkButton();
  if (WiFi.status()!=WL_CONNECTED) wifiMulti.run();
  if (Blynk.connected()) Blynk.run();
  timer.run();
  server.handleClient();
}
