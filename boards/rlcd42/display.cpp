// Display backend for the RLCD board: ST7305 reflective LCD, updates instantly.
#include "config.h"
#include "gfx_backend.h"
#include "ST7305_U8g2.h"

namespace gfx {
namespace backend {

static ST7305_U8g2 lcd(PIN_LCD_SCK, PIN_LCD_MOSI, PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_RST);

U8G2 *begin() {
  lcd.begin(0, U8G2_R1);
  return lcd.getU8g2();
}
void flush() { lcd.getU8g2()->sendBuffer(); }
void waitIdle(uint32_t) {}
void requestFullRefresh() {}
void sleep() { delay(100); }

}  // namespace backend
}  // namespace gfx
