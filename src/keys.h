#pragma once
#include <Arduino.h>

enum KeyMod : uint8_t { M_CTRL = 1, M_SHIFT = 2, M_ALT = 4 };

// Printable keys carry their Unicode code point; everything else is >= K_SPECIAL.
enum : uint32_t {
  K_NONE = 0,
  K_SPECIAL = 0x110000,
  K_ENTER,
  K_BACKSPACE,
  K_DELETE,
  K_TAB,
  K_ESC,
  K_LEFT,
  K_RIGHT,
  K_UP,
  K_DOWN,
  K_HOME,
  K_END,
  K_PGUP,
  K_PGDN,
  K_F1,
  K_F2,
  K_F3,
  K_F4,
  K_F5,
  K_F6,
  K_F7,
  K_F8,
  K_F9,
  K_F10,
  K_F11,
  K_F12,
};

struct Key {
  uint32_t code = K_NONE;
  uint8_t mods = 0;
  bool ctrl() const { return mods & M_CTRL; }
  bool shift() const { return mods & M_SHIFT; }
  bool alt() const { return mods & M_ALT; }
  bool printable() const { return code >= 32 && code < K_SPECIAL && !(mods & (M_CTRL | M_ALT)); }
  bool is(uint32_t c) const { return code == c; }
  bool ctrlKey(char c) const { return (mods & M_CTRL) && code == (uint32_t)c; }
};
