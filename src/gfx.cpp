#include "gfx.h"
#include "gfx_backend.h"

namespace gfx {
static U8G2 *g = nullptr;
static bool inv = false;

void begin() {
  g = backend::begin();
  g->setFontMode(1);
  g->setDrawColor(1);
}
void flush() { backend::flush(); }
void waitIdle(uint32_t timeoutMs) { backend::waitIdle(timeoutMs); }
void requestFullRefresh() { backend::requestFullRefresh(); }
void sleep() { backend::sleep(); }

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
