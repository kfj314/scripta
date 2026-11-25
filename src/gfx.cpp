#include "gfx.h"
#include "ST7305_U8g2.h"

namespace gfx {
static ST7305_U8g2 lcd(PIN_LCD_SCK, PIN_LCD_MOSI, PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_RST);
static U8G2 *g = nullptr;
static bool inv = false;

void begin() {
  lcd.begin(0, U8G2_R1);
  g = lcd.getU8g2();
  g->setFontMode(1);
  g->setDrawColor(1);
}
U8G2 &u() { return *g; }
void setInverted(bool v) { inv = v; }
bool inverted() { return inv; }
// 1 = pixel set by u8g2. With the default theme set pixels are the dark "ink".
uint8_t ink() { return inv ? 0 : 1; }
uint8_t paper() { return inv ? 1 : 0; }

void clear() {
  g->clearBuffer();
  if (inv) {
    g->setDrawColor(1);
    g->drawBox(0, 0, LCD_W, LCD_H);
  }
  g->setDrawColor(ink());
}
void flush() { g->sendBuffer(); }
void fillRect(int x, int y, int w, int h, bool isInk) {
  g->setDrawColor(isInk ? ink() : paper());
  g->drawBox(x, y, w, h);
  g->setDrawColor(ink());
}
void hline(int x, int y, int w) {
  g->setDrawColor(ink());
  g->drawHLine(x, y, w);
}
}  // namespace gfx
