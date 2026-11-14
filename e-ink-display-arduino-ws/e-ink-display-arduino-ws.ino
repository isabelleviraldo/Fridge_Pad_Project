#include <Wire.h>

// TP2 / P7 touch connector
#define TOUCH_SDA 32   // A4 / P7 pin 5
#define TOUCH_SCL 33   // A5 / P7 pin 6

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(100000);

  Serial.println();
  Serial.println("GT911 I2C scanner");
  Serial.println("TP2/P7:");
  Serial.println("SDA = GPIO32");
  Serial.println("SCL = GPIO33");
  Serial.println("Scanning...");
}

void loop() {
  byte found = 0;

  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("I2C device found at 0x");

      if (address < 16) {
        Serial.print("0");
      }

      Serial.println(address, HEX);
      found++;
    }
  }

  if (found == 0) {
    Serial.println("No I2C devices found.");
  }

  Serial.println("--------------------");
  delay(3000);
}