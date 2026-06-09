/* ============================================================
   Air Quality Monitor — ESP32
   + SSD1306 OLED display with 3 screens, button to cycle
   Screen 1: CO2 + AQI
   Screen 2: PM1.0 / PM2.5 / PM10
   Screen 3: Temp / Humidity / Pressure / Gas
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
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── Display ───────────────────────────────────────────────────────────────────
#define SCREEN_W   128
#define SCREEN_H    64
#define OLED_ADDR 0x3C   // try 0x3D if display stays blank
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

// ── Button ────────────────────────────────────────────────────────────────────
#define BTN_PIN     4    // GPIO4 → button → GND
int  currentScreen  = 0; // 0=CO2/AQI  1=Particles  2=Climate
bool lastBtnState   = HIGH;
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
Adafruit_BME680 bme;
WiFiMulti wifiMulti;
WebServer server(80);
BlynkTimer timer;

// ── Sensor globals ────────────────────────────────────────────────────────────
int32_t  g_co2   = 0;
uint16_t g_pm1   = 0, g_pm25 = 0, g_pm10 = 0;
float    g_temp  = 0, g_hum  = 0, g_pres = 0, g_gas = 0;
int      g_aqi   = 0;
String   g_aqiLabel  = "---";
String   g_co2Status = "warming up";
bool     g_pmsValid  = false;

// ── AQI ───────────────────────────────────────────────────────────────────────
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
String aqiLabel(int a) {
  if (a<=50)  return "Good";
  if (a<=100) return "Moderate";
  if (a<=150) return "USG";        // short for display
  if (a<=200) return "Unhealthy";
  if (a<=300) return "Very Bad";
  return "Hazardous";
}
String aqiColor(int a) {
  if (a<=50)  return "#00e400";
  if (a<=100) return "#ffff00";
  if (a<=150) return "#ff7e00";
  if (a<=200) return "#ff0000";
  if (a<=300) return "#8f3f97";
  return "#7e0023";
}

// ── MTP80-A ───────────────────────────────────────────────────────────────────
const uint8_t CO2_CMD[] = {0x42,0x4D,0xA0,0x00,0x03,0x00,0x00,0x01,0x32};
void readCO2() {
  while (mtp.available()) mtp.read();
  mtp.write(CO2_CMD, 9);
  uint32_t t = millis();
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
  g_pmsValid=true;
  g_aqi=max(aqiFromPM25(g_pm25), aqiFromPM10(g_pm10));
  g_aqiLabel=aqiLabel(g_aqi);
}

// ── BME680 ────────────────────────────────────────────────────────────────────
void readBME() {
  if (bme.performReading()) {
    g_temp=bme.temperature; g_hum=bme.humidity;
    g_pres=bme.pressure/100.0; g_gas=bme.gas_resistance/1000.0;
  }
}

// ── OLED Screens ──────────────────────────────────────────────────────────────

// Draws a small screen indicator at bottom right  e.g.  [1/3]
void drawScreenIndicator(int screen) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(98, 56);
  display.print("[");
  display.print(screen+1);
  display.print("/3]");
}

// Screen 0: CO2 and AQI
void drawScreen0() {
  display.clearDisplay();

  // Title bar
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(28, 2);
  display.print("CO2 & AQI");

  // CO2
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 16);
  display.print("CO2:");
  if (g_co2Status == "ok") {
    display.setTextSize(2);
    display.setCursor(0, 26);
    display.print(g_co2);
    display.setTextSize(1);
    display.print(" ppm");
  } else {
    display.setTextSize(1);
    display.setCursor(0, 26);
    display.print(g_co2Status);
  }

  // Divider
  display.drawFastHLine(0, 45, 128, SSD1306_WHITE);

  // AQI
  display.setTextSize(1);
  display.setCursor(0, 48);
  display.print("AQI:");
  display.setCursor(30, 48);
  display.print(g_aqi);
  display.setCursor(60, 48);
  display.print(g_aqiLabel);

  drawScreenIndicator(0);
  display.display();
}

// Screen 1: Particulates
void drawScreen1() {
  display.clearDisplay();

  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(16, 2);
  display.print("Particulates");

  display.setTextColor(SSD1306_WHITE);

  if (!g_pmsValid) {
    display.setTextSize(1);
    display.setCursor(10, 28);
    display.print("PMS7003 no data");
  } else {
    // PM1.0
    display.setTextSize(1);
    display.setCursor(0, 16);
    display.print("PM1.0:");
    display.setTextSize(2);
    display.setCursor(50, 13);
    display.print(g_pm1);
    display.setTextSize(1);
    display.print(" ug/m3");

    // PM2.5
    display.setTextSize(1);
    display.setCursor(0, 33);
    display.print("PM2.5:");
    display.setTextSize(2);
    display.setCursor(50, 30);
    display.print(g_pm25);
    display.setTextSize(1);
    display.print(" ug/m3");

    // PM10
    display.setTextSize(1);
    display.setCursor(0, 50);
    display.print("PM10 :");
    display.setTextSize(1);
    display.setCursor(50, 50);
    display.print(g_pm10);
    display.print(" ug/m3");
  }

  drawScreenIndicator(1);
  display.display();
}

// Screen 2: Climate (BME680)
void drawScreen2() {
  display.clearDisplay();

  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(28, 2);
  display.print("Climate");

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 15);
  display.print("Temp :");
  display.setCursor(50, 15);
  display.print(g_temp, 1);
  display.print(" C");

  display.setCursor(0, 26);
  display.print("Humid:");
  display.setCursor(50, 26);
  display.print(g_hum, 1);
  display.print(" %");

  display.setCursor(0, 37);
  display.print("Press:");
  display.setCursor(50, 37);
  display.print(g_pres, 1);
  display.print(" hPa");

  display.setCursor(0, 48);
  display.print("Gas  :");
  display.setCursor(50, 48);
  display.print(g_gas, 1);
  display.print(" kOhm");

  drawScreenIndicator(2);
  display.display();
}

void updateDisplay() {
  switch (currentScreen) {
    case 0: drawScreen0(); break;
    case 1: drawScreen1(); break;
    case 2: drawScreen2(); break;
  }
}

// ── Button check (non-blocking debounce) ──────────────────────────────────────
void checkButton() {
  bool state = digitalRead(BTN_PIN);
  if (state == LOW && lastBtnState == HIGH && millis()-lastDebounce > 200) {
    currentScreen = (currentScreen + 1) % 3;
    updateDisplay();
    lastDebounce = millis();
  }
  lastBtnState = state;
}

// ── Read all sensors ──────────────────────────────────────────────────────────
void readAllSensors() {
  readCO2(); readPMS(); readBME();
  updateDisplay();  // refresh display with new data
  Serial.printf("CO2:%dppm  PM2.5:%d  PM10:%d  AQI:%d(%s)\n",
                g_co2, g_pm25, g_pm10, g_aqi, g_aqiLabel.c_str());
  Serial.printf("Temp:%.1fC  Hum:%.1f%%  Pres:%.1fhPa  Gas:%.1fkOhm\n",
                g_temp, g_hum, g_pres, g_gas);
}

// ── Blynk ─────────────────────────────────────────────────────────────────────
void sendToBlynk() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (g_co2Status=="ok")  Blynk.virtualWrite(V0, g_co2);
  if (g_pmsValid) {
    Blynk.virtualWrite(V1, g_pm1);
    Blynk.virtualWrite(V2, g_pm25);
    Blynk.virtualWrite(V3, g_pm10);
    Blynk.virtualWrite(V8, g_aqi);
  }
  Blynk.virtualWrite(V4, g_temp);
  Blynk.virtualWrite(V5, g_hum);
  Blynk.virtualWrite(V6, g_pres);
  Blynk.virtualWrite(V7, g_gas);
}

// ── Web dashboard ─────────────────────────────────────────────────────────────
void handleRoot() {
  String ac = aqiColor(g_aqi);
  String html = R"rawhtml(<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="10">
<title>Air Quality Monitor</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:sans-serif;background:#1a1a2e;color:#eee;padding:16px}
h1{text-align:center;margin-bottom:16px;font-size:1.3em;color:#00d4ff}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.card{background:#16213e;border-radius:12px;padding:14px;text-align:center}
.card .label{font-size:.75em;color:#aaa;margin-bottom:4px}
.card .value{font-size:1.6em;font-weight:bold}
.card .unit{font-size:.75em;color:#aaa}
.aqi-card{grid-column:1/-1;border:2px solid )rawhtml";
  html += ac + ";}";
  html += ".aqi-card .value{color:" + ac + ";font-size:2.2em}";
  html += R"rawhtml(
.footer{text-align:center;margin-top:14px;font-size:.7em;color:#555}
</style></head><body>
<h1>🌿 Air Quality Monitor</h1><div class="grid">)rawhtml";

  html += "<div class='card aqi-card'><div class='label'>AQI</div><div class='value'>" + String(g_aqi) + "</div><div class='unit'>" + g_aqiLabel + "</div></div>";
  html += "<div class='card'><div class='label'>CO₂</div><div class='value'>" + String(g_co2) + "</div><div class='unit'>ppm</div></div>";
  html += "<div class='card'><div class='label'>PM2.5</div><div class='value'>" + String(g_pm25) + "</div><div class='unit'>µg/m³</div></div>";
  html += "<div class='card'><div class='label'>PM10</div><div class='value'>" + String(g_pm10) + "</div><div class='unit'>µg/m³</div></div>";
  html += "<div class='card'><div class='label'>PM1.0</div><div class='value'>" + String(g_pm1) + "</div><div class='unit'>µg/m³</div></div>";
  html += "<div class='card'><div class='label'>Temperature</div><div class='value'>" + String(g_temp,1) + "</div><div class='unit'>°C</div></div>";
  html += "<div class='card'><div class='label'>Humidity</div><div class='value'>" + String(g_hum,1) + "</div><div class='unit'>%</div></div>";
  html += "<div class='card'><div class='label'>Pressure</div><div class='value'>" + String(g_pres,1) + "</div><div class='unit'>hPa</div></div>";
  html += "<div class='card'><div class='label'>Gas (VOC)</div><div class='value'>" + String(g_gas,1) + "</div><div class='unit'>kΩ</div></div>";
  html += "</div><div class='footer'>Auto-refreshes every 10s | IP: " + WiFi.localIP().toString() + "</div></body></html>";
  server.send(200, "text/html", html);
}

void handleJSON() {
  String j = "{\"co2\":" + String(g_co2) + ",\"pm1\":" + String(g_pm1) +
             ",\"pm25\":" + String(g_pm25) + ",\"pm10\":" + String(g_pm10) +
             ",\"aqi\":" + String(g_aqi) + ",\"aqi_label\":\"" + g_aqiLabel +
             "\",\"temp\":" + String(g_temp,2) + ",\"humidity\":" + String(g_hum,2) +
             ",\"pressure\":" + String(g_pres,2) + ",\"gas\":" + String(g_gas,2) + "}";
  server.send(200, "application/json", j);
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  mtp.begin(9600, SERIAL_8N1, 16, 17);
  pms.begin(9600, SERIAL_8N1, 25, 26);
  Wire.begin(21, 22);

  pinMode(BTN_PIN, INPUT_PULLUP);

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 not found! Check wiring or try address 0x3D");
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(20, 20);
    display.print("Air Quality");
    display.setCursor(28, 35);
    display.print("Monitor");
    display.display();
    Serial.println("SSD1306 OK");
  }

  // BME680
  if (!bme.begin()) {
    Serial.println("BME680 not found!");
  } else {
    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(320, 150);
    Serial.println("BME680 OK");
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
    Serial.println("\nNo WiFi — AP only mode");
  }
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", handleRoot);
  server.on("/json", handleJSON);
  server.begin();

  // Timers
  timer.setInterval(10000L, readAllSensors);
  timer.setInterval(60000L, sendToBlynk);

  delay(1500); // show splash screen
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  checkButton();
  if (WiFi.status()!=WL_CONNECTED) wifiMulti.run();
  if (Blynk.connected()) Blynk.run();
  timer.run();
  server.handleClient();
}
