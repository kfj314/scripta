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
// Show the frame. On the LCD this is immediate. On e-paper it queues the frame
// for a background refresh and returns at once; frames that arrive while the
// panel is busy are merged, so only the latest one is drawn.
void flush();
// Block until every queued frame is on the panel (no-op on the LCD).
void waitIdle(uint32_t timeoutMs = 15000);
// Ask for the next e-paper refresh to be a full one (clears ghosting).
void requestFullRefresh();
// Finish drawing and power the panel down before deep sleep.
void sleep();
uint8_t ink();
uint8_t paper();
void fillRect(int x, int y, int w, int h, bool ink);
void hline(int x, int y, int w);
}  // namespace gfx
