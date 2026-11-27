#include "epd_ssd1677.h"
#include "config.h"
#include <SPI.h>
#include "power.h"

// Register sequence follows the SSD1677 datasheet and the values Waveshare's
// demo uses for this panel (booster soft-start, gate count, data entry mode,
// window). Data entry mode 0x01 = X increment, Y decrement, which is why the
// Y window is given end-first.

namespace epd {

static SPIClass spi(FSPI);
static const uint32_t kSpiHz = 10000000;
static const uint32_t kBusyTimeoutMs = 10000;

static void cmd(uint8_t c) {
  digitalWrite(PIN_EPD_DC, LOW);
  digitalWrite(PIN_EPD_CS, LOW);
  spi.transfer(c);
  digitalWrite(PIN_EPD_CS, HIGH);
}

static void data(uint8_t d) {
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  spi.transfer(d);
  digitalWrite(PIN_EPD_CS, HIGH);
}

static void data16(uint16_t v) {
  data(v & 0xFF);
  data((v >> 8) & 0xFF);
}

static void waitBusy() {
  delay(2);
  uint32_t t0 = millis();
  while (digitalRead(PIN_EPD_BUSY) == HIGH) {
    if (millis() - t0 > kBusyTimeoutMs) {
      Serial.println("[epd] busy timeout");
      return;
    }
    delay(5);
  }
}

static void hwReset() {
  digitalWrite(PIN_EPD_RST, HIGH);
  delay(20);
  digitalWrite(PIN_EPD_RST, LOW);
  delay(2);
  digitalWrite(PIN_EPD_RST, HIGH);
  delay(20);
}

// Streams rows [y0, y1), byte columns [xb0, xb1) of the frame, inverted
// (u8g2 1 = ink, panel 1 = white).
static void sendRegion(uint8_t ram, const uint8_t *frame, int xb0, int xb1, int y0, int y1) {
  static uint8_t chunk[512];
  size_t n = 0;
  cmd(ram);
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  for (int y = y0; y < y1; y++) {
    const uint8_t *row = frame + (size_t)y * ROW_BYTES;
    for (int x = xb0; x < xb1; x++) {
      chunk[n++] = ~row[x];
      if (n == sizeof(chunk)) {
        spi.transferBytes(chunk, nullptr, n);
        n = 0;
      }
    }
  }
  if (n) spi.transferBytes(chunk, nullptr, n);
  digitalWrite(PIN_EPD_CS, HIGH);
}

static void setWindow(int xb0, int xb1, int y0, int y1) {
  uint16_t xs = xb0 * 8, xe = xb1 * 8 - 1, ys = y0, ye = y1 - 1;
  cmd(0x44);  // RAM X start / end (pixels)
  data16(xs);
  data16(xe);
  cmd(0x45);  // RAM Y start / end (end first: Y decrements)
  data16(ye);
  data16(ys);
  cmd(0x4E);  // X counter
  data16(xs);
  cmd(0x4F);  // Y counter
  data16(ys);
}

static void initFull() {
  power::displayRail(true);
  delay(10);
  hwReset();
  waitBusy();
  cmd(0x12);  // software reset
  waitBusy();
  cmd(0x18);  // internal temperature sensor
  data(0x80);
  cmd(0x0C);  // booster soft-start
  data(0xAE);
  data(0xC7);
  data(0xC3);
  data(0xC0);
  data(0x80);
  cmd(0x01);  // driver output control: gate count, scan direction
  data16(H - 1);
  data(0x02);
  cmd(0x3C);  // border waveform
  data(0x01);
  cmd(0x11);  // data entry mode
  data(0x01);
  setWindow(0, ROW_BYTES, 0, H);
  waitBusy();
}

void begin() {
  pinMode(PIN_EPD_CS, OUTPUT);
  pinMode(PIN_EPD_DC, OUTPUT);
  pinMode(PIN_EPD_RST, OUTPUT);
  pinMode(PIN_EPD_BUSY, INPUT);
  digitalWrite(PIN_EPD_CS, HIGH);
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_RST, HIGH);
  spi.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, -1);
  spi.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));
}

void full(const uint8_t *frame) {
  initFull();
  sendRegion(0x24, frame, 0, ROW_BYTES, 0, H);  // new image
  setWindow(0, ROW_BYTES, 0, H);
  sendRegion(0x26, frame, 0, ROW_BYTES, 0, H);  // "previous" image for partials
  cmd(0x22);
  data(0xF7);  // display mode 1 (full waveform)
  cmd(0x20);
  waitBusy();
}

void partial(const uint8_t *frame, int xb0, int xb1, int y0, int y1) {
  hwReset();
  cmd(0x18);
  data(0x80);
  cmd(0x3C);  // border: keep (no flashing frame)
  data(0x80);
  setWindow(xb0, xb1, y0, y1);
  sendRegion(0x24, frame, xb0, xb1, y0, y1);
  cmd(0x22);
  data(0xFF);  // display mode 2 (partial waveform: old RAM 0x26 -> new RAM 0x24)
  cmd(0x20);
  waitBusy();
  // Make RAM 0x26 match what is now on the glass, so the next partial
  // compares against the right "before" image.
  setWindow(xb0, xb1, y0, y1);
  sendRegion(0x26, frame, xb0, xb1, y0, y1);
}

void sleep() {
  cmd(0x10);  // deep sleep mode 1 (RAM kept)
  data(0x01);
  delay(10);
  power::displayRail(false);
}

}  // namespace epd
