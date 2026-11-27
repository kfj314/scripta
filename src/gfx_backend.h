#pragma once
#include <U8g2lib.h>

// What each board's display code (boards/<name>/display.cpp) provides to gfx.
// gfx.cpp holds everything that is the same on every panel.
namespace gfx {
namespace backend {
U8G2 *begin();          // set up the panel; returns the canvas to draw into
void flush();           // show the canvas
void waitIdle(uint32_t timeoutMs);
void requestFullRefresh();
void sleep();           // before deep sleep
}  // namespace backend
}  // namespace gfx
