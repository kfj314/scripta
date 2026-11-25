#pragma once
#include <Arduino.h>

// Font faces:
//  - three built-in u8g2 bitmap families (Serif, Sans, Mono; six sizes; real
//    bold; italics are produced by shearing the bitmap)
//  - TTF families embedded in the firmware (Averia Serif, Quicksand) with their
//    own bold/italic files where they exist
//  - any family found as Name-Regular/Bold/Italic/BoldItalic.ttf in /fonts on
//    the card. Missing bold/italic styles are synthesised.
namespace fonts {

constexpr int LEVELS = 6;  // size steps; 0 smallest
enum Variant : uint8_t { V_REG = 0, V_BOLD = 1, V_ITAL = 2 };  // bit flags

struct Ref {
  int face;
  int level;
};

void begin();
void rescan();  // re-read /fonts (call after SD is mounted)
int faceCount();
const char *faceName(int i);
int faceIndex(const char *name);  // falls back to 0
bool hasItalic(const Ref &r);     // true when italics are drawn as real slanted glyphs

int adv(const Ref &r, uint8_t variant, uint32_t cp);
void draw(const Ref &r, uint8_t variant, int x, int baseline, uint32_t cp);
int ascent(const Ref &r);
int lineHeight(const Ref &r);

}  // namespace fonts
