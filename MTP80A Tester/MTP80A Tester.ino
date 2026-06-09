/**
 * MTP80-A NDIR CO2 Sensor — ESP32 Reader
 * =======================================
 * Protocol: UART, 9600 baud, 3.3 V logic
 *
 * WIRING (ESP32 DevKit → MTP80-A):
 *   ESP32 3.3 V  ────────  Pin 5 (VCC-Out) ← NOT used for power here
 *   ESP32 5 V    ────────  Pin 1 (VIN)
 *   ESP32 GND    ────────  Pin 2 (GND)
 *   ESP32 GPIO17 (TX2) ──  Pin 6 (Host-TX / IIC-SDA)   ← sensor RX
 *   ESP32 GPIO16 (RX2) ──  Pin 7 (Host-RX / IIC-SCL)   ← sensor TX
 *   Pin 8 (R/T)  ────────  Leave FLOATING (= UART mode, NOT I2C)
 *
 * NOTE: MTP80-A UART is 3.3 V logic. ESP32 GPIO is 3.3 V → safe direct connect.
 * Do NOT connect sensor TX directly to a 5 V Arduino without a level shifter.
 *
 * PROTOCOL SUMMARY (NOT the same as MHZ-19C!):
 *   Send 9 bytes:  42 4D A0 00 03 00 00 01 32
 *   Receive 14 bytes: 42 4D A0 00 03 00 05 [B0 B1 B2 B3] [VALID] [CK1 CK2]
 *   CO2 ppm = (B0<<24)|(B1<<16)|(B2<<8)|B3   (32-bit big-endian)
 *   VALID   = 0x00 → data OK,  0xFF → sensor not ready yet
 */

#include <Arduino.h>
#include <HardwareSerial.h>

// ── Configuration ────────────────────────────────────────────────────────────
#define SENSOR_SERIAL   Serial2
#define SENSOR_BAUD     9600
#define RX_PIN          16      // GPIO16 = RX2 on most ESP32 DevKit boards
#define TX_PIN          17      // GPIO17 = TX2

#define READ_INTERVAL_MS  5000  // How often to request a new reading (ms)
// ─────────────────────────────────────────────────────────────────────────────

// Command to read CO2 concentration  (9 bytes, checksum included)
// Frame: 42 4D | A0 | 00 03 | 00 00 | checksum(01 32)
const uint8_t CMD_READ_CO2[9] = {0x42, 0x4D, 0xA0, 0x00, 0x03, 0x00, 0x00, 0x01, 0x32};

// Expected response length = 2(hdr)+1(instr)+2(cmd)+2(dlen)+5(data)+2(cksum) = 14
#define RESP_LEN 14

// ── Globals ──────────────────────────────────────────────────────────────────
uint8_t  rxBuf[RESP_LEN];
uint32_t lastReadTime = 0;
bool     sensorReady  = false;

// ── Helpers ──────────────────────────────────────────────────────────────────

/**
 * Calculates the 16-bit checksum expected for a received frame.
 * Checksum = sum of bytes from index 0 to (len-3) inclusive.
 */
uint16_t calcChecksum(const uint8_t* buf, int len) {
    uint32_t sum = 0;
    for (int i = 0; i < len - 2; i++) sum += buf[i];
    return (uint16_t)(sum & 0xFFFF);
}

/**
 * Sends the read-CO2 command and waits for a 14-byte response.
 * Returns true if a valid, checksummed response is received.
 * Populates co2_ppm with the reading.
 */
bool readCO2(int32_t &co2_ppm, bool &dataValid) {
    // Flush any stale bytes
    while (SENSOR_SERIAL.available()) SENSOR_SERIAL.read();

    // Send command
    SENSOR_SERIAL.write(CMD_READ_CO2, sizeof(CMD_READ_CO2));
    SENSOR_SERIAL.flush();

    // Wait for response (up to 500 ms)
    uint32_t t0 = millis();
    int idx = 0;

    while (idx < RESP_LEN) {
        if (millis() - t0 > 500) {
            Serial.println("[MTP80-A] Timeout waiting for response!");
            return false;
        }
        if (SENSOR_SERIAL.available()) {
            rxBuf[idx++] = SENSOR_SERIAL.read();
        }
    }

    // Validate header
    if (rxBuf[0] != 0x42 || rxBuf[1] != 0x4D) {
        Serial.printf("[MTP80-A] Bad header: %02X %02X\n", rxBuf[0], rxBuf[1]);
        return false;
    }

    // Validate checksum
    uint16_t received_ck = ((uint16_t)rxBuf[RESP_LEN-2] << 8) | rxBuf[RESP_LEN-1];
    uint16_t computed_ck = calcChecksum(rxBuf, RESP_LEN);
    if (received_ck != computed_ck) {
        Serial.printf("[MTP80-A] Checksum mismatch: got %04X, expected %04X\n",
                      received_ck, computed_ck);
        return false;
    }

    // Data starts at byte index 7 (after 2+1+2+2 header bytes)
    // Bytes 7-10: CO2 value (32-bit big-endian)
    // Byte  11  : validity flag
    co2_ppm = ((int32_t)rxBuf[7] << 24) |
              ((int32_t)rxBuf[8] << 16) |
              ((int32_t)rxBuf[9] << 8)  |
               (int32_t)rxBuf[10];

    dataValid = (rxBuf[11] == 0x00);
    return true;
}

// ── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== MTP80-A CO2 Sensor (ESP32) ===");
    Serial.println("Initialising UART2...");

    SENSOR_SERIAL.begin(SENSOR_BAUD, SERIAL_8N1, RX_PIN, TX_PIN);
    delay(100);

    Serial.println("Sensor warm-up: wait ~90 seconds for first valid T90 reading.");
    Serial.println("Readings will be requested every " + String(READ_INTERVAL_MS/1000) + " seconds.\n");
}

// ── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
  // Add this at the very top of loop(), before the timer check
if (SENSOR_SERIAL.available()) {
    Serial.print("RAW byte: 0x");
    Serial.println(SENSOR_SERIAL.read(), HEX);
}
    uint32_t now = millis();

    if (now - lastReadTime >= READ_INTERVAL_MS) {
        lastReadTime = now;

        int32_t co2  = 0;
        bool    valid = false;

        if (readCO2(co2, valid)) {
            if (!valid) {
                Serial.println("[MTP80-A] Sensor warming up — data not yet valid (0xFF). Please wait.");
            } else {
                Serial.printf("[MTP80-A] CO2 = %d ppm", co2);

                // Simple air quality label
                if      (co2 < 800)  Serial.print("  → Good (fresh air)");
                else if (co2 < 1000) Serial.print("  → Acceptable");
                else if (co2 < 1500) Serial.print("  ⚠ Elevated — open a window");
                else if (co2 < 2000) Serial.print("  ⚠ High — ventilate now");
                else                 Serial.print("  ✗ Very High — evacuate / ventilate urgently");

                Serial.println();
                sensorReady = true;
            }
        }
        // else: error printed inside readCO2()
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
   OPTIONAL ADVANCED COMMANDS
   (uncomment / call from setup() as needed)
   ─────────────────────────────────────────────────────────────────────────────

// Disable auto-calibration (useful if sensor is always indoors):
void disableAutoCalibration() {
    // 42 4D A0 00 06 00 01 FF checksum
    uint8_t cmd[] = {0x42, 0x4D, 0xA0, 0x00, 0x06, 0x00, 0x01, 0xFF};
    uint16_t ck = 0;
    for (int i=0;i<8;i++) ck += cmd[i];
    SENSOR_SERIAL.write(cmd, 8);
    SENSOR_SERIAL.write((uint8_t)(ck >> 8));
    SENSOR_SERIAL.write((uint8_t)(ck & 0xFF));
    delay(100);
}

// Set auto-calibration period to 24 hours:
void setAutoCalPeriod(uint16_t hours) {  // valid range: 24–720
    uint8_t cmd[] = {0x42, 0x4D, 0xA0, 0x00, 0x09, 0x00, 0x02,
                     (uint8_t)(hours >> 8), (uint8_t)(hours & 0xFF)};
    uint16_t ck = 0;
    for (int i=0;i<9;i++) ck += cmd[i];
    SENSOR_SERIAL.write(cmd, 9);
    SENSOR_SERIAL.write((uint8_t)(ck >> 8));
    SENSOR_SERIAL.write((uint8_t)(ck & 0xFF));
    delay(100);
}

   ─────────────────────────────────────────────────────────────────────────── */