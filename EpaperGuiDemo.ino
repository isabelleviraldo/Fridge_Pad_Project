#include "GT911.h"
#include "i2c.h"
#include <SPI.h>
#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <pgmspace.h>

// ESP32-M1 / TP2 pin mapping
const int IRQ_Pin  = 36;  // TP2 INT / A0
const int BUSY_Pin = 13;  // e-paper BUSY / A14
const int RES_Pin  = 12;  // e-paper RESET / A15
const int DC_Pin   = 14;  // e-paper DC / A16
const int CS_Pin   = 27;  // e-paper CS / A17

#define EPD_CS_LOW()   digitalWrite(CS_Pin, LOW)
#define EPD_CS_HIGH()  digitalWrite(CS_Pin, HIGH)
#define EPD_DC_LOW()   digitalWrite(DC_Pin, LOW)
#define EPD_DC_HIGH()  digitalWrite(DC_Pin, HIGH)
#define EPD_RST_LOW()  digitalWrite(RES_Pin, LOW)
#define EPD_RST_HIGH() digitalWrite(RES_Pin, HIGH)

const int SCREEN_W = 800;
const int SCREEN_H = 480;
const int BYTES_PER_ROW = SCREEN_W / 8;
const int FRAME_BYTES = BYTES_PER_ROW * SCREEN_H;

// One bit per pixel. A 1 is drawn as black by this sketch.
uint8_t frame[FRAME_BYTES];

uint8_t currentPage = 0; // 0 = Hello, 1 = Calculator
int calculatorValue = 0;
bool redrawNeeded = true;
unsigned long lastTouch = 0;

// -----------------------------------------------------------------------------
// Small 5x7 font. This is intentionally simple so the same GUI can be ported
// to a computer canvas, an ESP32 framebuffer, or another e-paper controller.
// -----------------------------------------------------------------------------
const uint8_t font[] PROGMEM = {
  // A-Z, five columns each
  0x7E,0x09,0x09,0x09,0x7E,  0x7F,0x49,0x49,0x49,0x36,
  0x3E,0x41,0x41,0x41,0x22,  0x7F,0x41,0x41,0x22,0x1C,
  0x7F,0x49,0x49,0x49,0x41,  0x7F,0x09,0x09,0x09,0x01,
  0x3E,0x41,0x49,0x49,0x7A,  0x7F,0x08,0x08,0x08,0x7F,
  0x00,0x41,0x7F,0x41,0x00,  0x20,0x40,0x41,0x3F,0x01,
  0x7F,0x08,0x14,0x22,0x41,  0x7F,0x40,0x40,0x40,0x40,
  0x7F,0x02,0x0C,0x02,0x7F,  0x7F,0x04,0x08,0x10,0x7F,
  0x3E,0x41,0x41,0x41,0x3E,  0x7F,0x09,0x09,0x09,0x06,
  0x3E,0x41,0x51,0x21,0x5E,  0x7F,0x09,0x19,0x29,0x46,
  0x46,0x49,0x49,0x49,0x31,  0x01,0x01,0x7F,0x01,0x01,
  0x3F,0x40,0x40,0x40,0x3F,  0x1F,0x20,0x40,0x20,0x1F,
  0x7F,0x20,0x18,0x20,0x7F,  0x63,0x14,0x08,0x14,0x63,
  0x07,0x08,0x70,0x08,0x07,  0x61,0x51,0x49,0x45,0x43,
  // 0-9
  0x3E,0x45,0x49,0x51,0x3E,  0x00,0x41,0x7F,0x40,0x00,
  0x42,0x61,0x51,0x49,0x46,  0x21,0x41,0x45,0x4B,0x31,
  0x18,0x14,0x12,0x7F,0x10,  0x27,0x45,0x45,0x45,0x39,
  0x3C,0x4A,0x49,0x49,0x30,  0x01,0x71,0x09,0x05,0x03,
  0x36,0x49,0x49,0x49,0x36,  0x06,0x49,0x49,0x29,0x1E
};

void clearFrame() {
  memset(frame, 0, sizeof(frame));
}

void setPixel(int x, int y, bool black = true) {
  if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
  uint8_t mask = 0x80 >> (x & 7);
  if (black) frame[y * BYTES_PER_ROW + (x >> 3)] |= mask;
  else       frame[y * BYTES_PER_ROW + (x >> 3)] &= ~mask;
}

void drawRect(int x, int y, int w, int h, bool filled = false) {
  if (filled) {
    for (int yy = y; yy < y + h; yy++)
      for (int xx = x; xx < x + w; xx++) setPixel(xx, yy);
  } else {
    for (int xx = x; xx < x + w; xx++) {
      setPixel(xx, y);
      setPixel(xx, y + h - 1);
    }
    for (int yy = y; yy < y + h; yy++) {
      setPixel(x, yy);
      setPixel(x + w - 1, yy);
    }
  }
}

int glyphIndex(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= '0' && c <= '9') return 26 + c - '0';
  return -1;
}

void drawChar(int x, int y, char c, int scale = 3) {
  if (c == '-' || c == '+') {
    int mid = y + 3 * scale;
    for (int xx = 0; xx < 5 * scale; xx++) setPixel(x + xx, mid);
    if (c == '+') {
      for (int yy = 0; yy < 7 * scale; yy++) setPixel(x + 2 * scale, y + yy);
    }
    return;
  }
  int index = glyphIndex(c);
  if (index < 0) return;
  for (int col = 0; col < 5; col++) {
    uint8_t bits = pgm_read_byte(&font[index * 5 + col]);
    for (int row = 0; row < 7; row++) {
      if (bits & (1 << row)) {
        for (int dx = 0; dx < scale; dx++)
          for (int dy = 0; dy < scale; dy++)
            setPixel(x + col * scale + dx, y + row * scale + dy);
      }
    }
  }
}

void drawText(int x, int y, const char *s, int scale = 3) {
  while (*s) {
    if (*s == ' ') x += 6 * scale;
    else {
      drawChar(x, y, *s, scale);
      x += 6 * scale;
    }
    s++;
  }
}

int textWidth(const char *s, int scale) {
  int width = 0;
  while (*s) {
    width += 6 * scale;
    s++;
  }
  return width;
}

void drawCenteredText(int y, const char *s, int scale) {
  drawText((SCREEN_W - textWidth(s, scale)) / 2, y, s, scale);
}

void drawButton(int x, int y, int w, int h, const char *label, int scale = 3) {
  drawRect(x, y, w, h, false);
  int tx = x + (w - textWidth(label, scale)) / 2;
  int ty = y + (h - 7 * scale) / 2;
  drawText(tx, ty, label, scale);
}

void drawGui() {
  clearFrame();

  // Header. It is outlined so the simple black-only font remains visible.
  drawRect(0, 0, SCREEN_W, 72, false);
  drawText(24, 22, "EPAPER PAD", 3);

  if (currentPage == 0) {
    drawCenteredText(125, "HELLO", 5);
    drawCenteredText(190, "EPAPER PAD", 3);
    drawButton(130, 255, 220, 72, "BUTTON 1", 3);
    drawButton(450, 255, 220, 72, "BUTTON 2", 3);
  } else {
    drawCenteredText(110, "CALCULATOR", 4);
    drawRect(285, 165, 230, 80, false);

    char valueText[12];
    snprintf(valueText, sizeof(valueText), "%d", calculatorValue);
    drawCenteredText(180, valueText, 5);

    drawButton(145, 275, 180, 72, "MINUS", 3);
    drawButton(475, 275, 180, 72, "PLUS", 3);
  }

  drawButton(20, 390, 180, 58, "HELLO", 2);
  drawButton(220, 390, 220, 58, "CALCULATOR", 2);
}

// -----------------------------------------------------------------------------
// UC8179 e-paper driver for the GDEY075T7-T01.
// -----------------------------------------------------------------------------
void SPI_Write(uint8_t value) { SPI.transfer(value); }

void EPD_WriteCommand(uint8_t command) {
  EPD_CS_LOW();
  EPD_DC_LOW();
  SPI_Write(command);
  EPD_CS_HIGH();
}

void EPD_WriteData(uint8_t data) {
  EPD_CS_LOW();
  EPD_DC_HIGH();
  SPI_Write(data);
  EPD_CS_HIGH();
}

void waitEPDReady() {
  while (digitalRead(BUSY_Pin) == LOW) delay(1);
}

void resetEPD() {
  EPD_RST_LOW();
  delay(10);
  EPD_RST_HIGH();
  delay(10);
}

void initEPD() {
  resetEPD();

  EPD_WriteCommand(0x00);
  EPD_WriteData(0x1F);

  EPD_WriteCommand(0x50);
  EPD_WriteData(0x10);
  EPD_WriteData(0x07);

  EPD_WriteCommand(0x04);
  delay(100);
  waitEPDReady();

  EPD_WriteCommand(0x06);
  EPD_WriteData(0x27);
  EPD_WriteData(0x27);
  EPD_WriteData(0x18);
  EPD_WriteData(0x17);

  EPD_WriteCommand(0xE0);
  EPD_WriteData(0x02);
  EPD_WriteCommand(0xE5);
  EPD_WriteData(0x5A);
}

void displayFrame() {
  // Old image
  EPD_WriteCommand(0x10);
  for (int i = 0; i < FRAME_BYTES; i++) EPD_WriteData(0xFF);

  // New image
  EPD_WriteCommand(0x13);
  for (int i = 0; i < FRAME_BYTES; i++) EPD_WriteData(frame[i]);

  EPD_WriteCommand(0x12);
  delay(10);
  waitEPDReady();
}

// -----------------------------------------------------------------------------
// Touch handling. The existing GT911 support files already use TP2/P7:
// SDA GPIO32, SCL GPIO33, INT GPIO36, RST GPIO39.
// -----------------------------------------------------------------------------
bool inBox(uint16_t x, uint16_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
}

void handleTouch(uint16_t x, uint16_t y) {
  if (millis() - lastTouch < 350) return;
  lastTouch = millis();

  if (inBox(x, y, 20, 390, 180, 58)) {
    currentPage = 0;
    redrawNeeded = true;
  } else if (inBox(x, y, 220, 390, 220, 58)) {
    currentPage = 1;
    redrawNeeded = true;
  } else if (currentPage == 1 && inBox(x, y, 145, 275, 180, 72)) {
    calculatorValue--;
    redrawNeeded = true;
  } else if (currentPage == 1 && inBox(x, y, 475, 275, 180, 72)) {
    calculatorValue++;
    redrawNeeded = true;
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(IRQ_Pin, INPUT);
  pinMode(BUSY_Pin, INPUT);
  pinMode(RES_Pin, OUTPUT);
  pinMode(DC_Pin, OUTPUT);
  pinMode(CS_Pin, OUTPUT);

  digitalWrite(CS_Pin, HIGH);
  digitalWrite(RES_Pin, HIGH);

  SPI.begin();
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));

  GT9XX_Initial();
  initEPD();

  drawGui();
  displayFrame();

  Serial.println("E-paper GUI ready");
}

void loop() {
  if (redrawNeeded) {
    drawGui();
    displayFrame();
    redrawNeeded = false;
  }

  // GT911 INT goes low when touch data is available.
  if (digitalRead(IRQ_Pin) == LOW) {
    TPR_Structure.TouchSta |= TP_COORD_UD;
    gt910_isr();

    if (touch_num > 0) {
      uint16_t x = TPR_Structure.x[0];
      uint16_t y = TPR_Structure.y[0];

      Serial.print("Touch X=");
      Serial.print(x);
      Serial.print(" Y=");
      Serial.println(y);

      handleTouch(x, y);
    }

    TPR_Structure.TouchSta = 0;
    for (int i = 0; i < 5; i++) {
      TPR_Structure.x[i] = 0;
      TPR_Structure.y[i] = 0;
    }

    delay(30);
  }
}
