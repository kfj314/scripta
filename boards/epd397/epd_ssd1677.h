#pragma once
#include <Arduino.h>

// Low-level driver for the 800x480 SSD1677-family panel on the
// Waveshare ESP32-S3-ePaper-3.97. Frames are 1 bit per pixel, row-major,
// MSB = leftmost pixel, 100 bytes per row, bit set = ink (black). This is the
// u8g2 "horizontal" buffer layout; the driver inverts on the way out because
// the panel uses 1 = white.
//
// All calls block until the panel is no longer busy, so call them from a
// background task (see gfx_epd.cpp).
namespace epd {

constexpr int W = 800;
constexpr int H = 480;
constexpr int ROW_BYTES = W / 8;
constexpr size_t FRAME_BYTES = ROW_BYTES * H;

void begin();
// Full refresh (~3 s, flashes). Clears ghosting. Re-initialises the panel.
void full(const uint8_t *frame);
// Partial refresh of rows [y0, y1) and byte columns [xb0, xb1).
void partial(const uint8_t *frame, int xb0, int xb1, int y0, int y1);
// Deep sleep (keeps the image). The next full() wakes it.
void sleep();

}  // namespace epd
