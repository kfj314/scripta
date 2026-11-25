#pragma once
#include <U8g2lib.h>
#include "config.h"

namespace gfx {
void begin();
U8G2 &u();
void setInverted(bool inv);
bool inverted();
// Clear the whole frame to the paper colour and select the ink colour.
void clear();
void flush();
uint8_t ink();
uint8_t paper();
void fillRect(int x, int y, int w, int h, bool ink);
void hline(int x, int y, int w);
}  // namespace gfx
