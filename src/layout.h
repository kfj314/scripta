#pragma once
#include <U8g2lib.h>
#include "config.h"

// Screen layouts are written in "design units" for the original 400x300
// panel. D() turns a design length into device pixels and SF() swaps a u8g2
// font for its scaled equivalent, so the same drawing code fits any panel.
// On the RLCD board both are identity.

// The board sets LAYOUT_SCALE_NUM / LAYOUT_SCALE_DEN in its board.h.
#ifndef LAYOUT_SCALE_NUM
#define LAYOUT_SCALE_NUM 1
#define LAYOUT_SCALE_DEN 1
#endif
constexpr int kScaleNum = LAYOUT_SCALE_NUM, kScaleDen = LAYOUT_SCALE_DEN;

constexpr int D(int v) { return v * kScaleNum / kScaleDen; }

// Scaled stand-in for a design font (any font not in the table is returned
// unchanged).
const uint8_t *SF(const uint8_t *designFont);
