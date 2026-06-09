#define BLYNK_TEMPLATE_ID   "TMPL39HRvkpe5"
#define BLYNK_TEMPLATE_NAME "Quickstart Template"
#define BLYNK_AUTH_TOKEN    "ZgxFiMwCYXHQp_YwDsfMvhSL-VhzX6wt"
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>

char ssid[] = "Prakyath pg 2 floor ";
char pass[] = "Kgs@12342";

char ssid2[] = "Sup";
char pass2[] = "12345678";

HardwareSerial mtp(2);
HardwareSerial pms(1);
Adafruit_BME680 bme;
BlynkTimer timer;

// V0=CO2, V1=PM1, V2=PM2.5, V3=PM10, V4=Temp, V5=Hum, V6=Pres, V7=Gas, V8=AQI

// ── AQI Calculator (US EPA, PM2.5 + PM10, returns higher of the two) ──────────
int aqiFromPM25(float pm) {
  // {C_low, C_high, AQI_low, AQI_high}
  float bl[][4] = {
    {0.0,   12.0,   0,   50},
    {12.1,  35.4,  51,  100},
    {35.5,  55.4, 101,  150},
    {55.5, 150.4, 151,  200},
    {150.5,250.4, 201,  300},
    {250.5,350.4, 301,  400},
    {350.5,500.4, 401,  500}
  };
  for (auto& b : bl) {
    if (pm <= b[1]) {
      return (int)((b[3]-b[2])/(b[1]-b[0]) * (pm - b[0]) + b[2]);
    }
  }
  return 500;
}

int aqiFromPM10(float pm) {
  float bl[][4] = {
    {0,   54,   0,  50},
    {55,  154,  51, 100},
    {155, 254, 101, 150},
    {255, 354, 151, 200},
    {355, 424, 201, 300},
    {425, 504, 301, 400},
    {505, 604, 401, 500}
  };
  for (auto& b : bl) {
    if (pm <= b[1]) {
      return (int)((b[3]-b[2])/(b[1]-b[0]) * (pm - b[0]) + b[2]);
    }
  }
  return 500;
}

String aqiLabel(int aqi) {
  if (aqi <= 50)  return "Good";
  if (aqi <= 100) return "Moderate";
  if (aqi <= 150) return "Unhealthy for Sensitive";
  if (aqi <= 200) return "Unhealthy";
  if (aqi <= 300) return "Very Unhealthy";
  return "Hazardous";
}

// ── MTP80-A ───────────────────────────────────────────────────────────────────
const uint8_t CO2_CMD[] = {0x42, 0x4D, 0xA0, 0x00, 0x03, 0x00, 0x00, 0x01, 0x32};

int32_t readCO2() {
  while (mtp.available()) mtp.read();
  mtp.write(CO2_CMD, 9);
  uint32_t t = millis();
  while (mtp.available() < 14) {
    if (millis() - t > 1000) return -1;
  }
  uint8_t buf[14];
  for (int i = 0; i < 14; i++) buf[i] = mtp.read();
  if (buf[0] != 0x42 || buf[1] != 0x4D) return -1;
  if (buf[11] != 0x00) return -2;
  return ((int32_t)buf[7] << 24) | ((int32_t)buf[8] << 16)
       | ((int32_t)buf[9] << 8)  |  (int32_t)buf[10];
}

// ── PMS7003 ───────────────────────────────────────────────────────────────────
struct PMS { uint16_t pm1, pm25, pm10; bool valid = false; };

struct PMS readPMS() {
  PMS data;
  while (pms.available()) pms.read();
  uint32_t t = millis();
  uint8_t buf[32];
  int idx = 0;
  while (millis() - t < 2000) {
    if (pms.available()) {
      uint8_t b = pms.read();
      if (idx == 0 && b != 0x42) continue;
      if (idx == 1 && b != 0x4D) { idx = 0; continue; }
      buf[idx++] = b;
      if (idx == 32) break;
    }
  }
  if (idx < 32) return data;
  uint16_t sum = 0;
  for (int i = 0; i < 30; i++) sum += buf[i];
  if (sum != (((uint16_t)buf[30] << 8) | buf[31])) return data;
  data.pm1  = ((uint16_t)buf[10] << 8) | buf[11];
  data.pm25 = ((uint16_t)buf[12] << 8) | buf[13];
  data.pm10 = ((uint16_t)buf[14] << 8) | buf[15];
  data.valid = true;
  return data;
}

// ── Main read + send ──────────────────────────────────────────────────────────
void sendSensorData() {

  // CO2
  int32_t co2 = readCO2();
  if (co2 > 0) {
    Serial.printf("CO2: %d ppm\n", co2);
    Blynk.virtualWrite(V0, co2);
  } else {
    Serial.println(co2 == -2 ? "CO2: warming up..." : "CO2: no response");
  }

  // PMS7003 + AQI
  PMS p = readPMS();
  if (p.valid) {
    int aqi = max(aqiFromPM25(p.pm25), aqiFromPM10(p.pm10));
    Serial.printf("PM1.0:%d  PM2.5:%d  PM10:%d µg/m³\n", p.pm1, p.pm25, p.pm10);
    Serial.printf("AQI: %d (%s)\n", aqi, aqiLabel(aqi).c_str());
    Blynk.virtualWrite(V1, p.pm1);
    Blynk.virtualWrite(V2, p.pm25);
    Blynk.virtualWrite(V3, p.pm10);
    Blynk.virtualWrite(V8, aqi);
  } else {
    Serial.println("PMS7003: no valid frame");
  }

  // BME680
  if (bme.performReading()) {
    Serial.printf("Temp:%.1f°C  Hum:%.1f%%  Pres:%.1fhPa  Gas:%.1fkΩ\n",
                  bme.temperature, bme.humidity,
                  bme.pressure/100.0, bme.gas_resistance/1000.0);
    Blynk.virtualWrite(V4, bme.temperature);
    Blynk.virtualWrite(V5, bme.humidity);
    Blynk.virtualWrite(V6, bme.pressure / 100.0);
    Blynk.virtualWrite(V7, bme.gas_resistance / 1000.0);
  } else {
    Serial.println("BME680: read failed");
  }

  Serial.println("────────────────────────");
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  mtp.begin(9600, SERIAL_8N1, 16, 17);
  pms.begin(9600, SERIAL_8N1, 25, 26);
  Wire.begin(21, 22);

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

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  timer.setInterval(10000L, sendSensorData);
}

void loop() {
  Blynk.run();
  timer.run();
}