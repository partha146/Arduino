#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_W 128
#define SCREEN_H 64
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Wire.begin(21, 22);
  Serial.println("\n\n=== OLED TEST ===");
  Serial.println("Testing I2C on GPIO21 (SDA) and GPIO22 (SCL)");
  
  // Scan for device
  Serial.print("Scanning I2C... ");
  byte found = 0;
  for (byte a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print("Found at 0x");
      Serial.print(a, HEX);
      Serial.print(" ");
      found++;
    }
  }
  Serial.println("\n");
  
  if (found == 0) {
    Serial.println("ERROR: No I2C devices found!");
    Serial.println("Check wiring: SDA→GPIO21, SCL→GPIO22, VCC→3.3V, GND→GND");
    while(1);
  }
  
  // Try to init display
  Serial.println("Initializing SSD1306...");
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("ERROR: SSD1306 not found at 0x3C");
    Serial.println("Trying 0x3D...");
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println("ERROR: SSD1306 failed at both 0x3C and 0x3D");
      Serial.println("Check: power, wiring, or try different I2C address");
      while(1);
    } else {
      Serial.println("SUCCESS: Found at 0x3D!");
    }
  } else {
    Serial.println("SUCCESS: SSD1306 initialized at 0x3C");
  }
  
  // Test display
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("OLED TEST");
  display.println("---");
  display.println("If you see this");
  display.println("text on display,");
  display.println("it WORKS!");
  display.display();
  
  Serial.println("Display test sent!");
  Serial.println("Check your OLED screen now...");
}

void loop() {
  delay(1000);
}