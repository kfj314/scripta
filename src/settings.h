#pragma once
#include <Arduino.h>

enum DocType : uint8_t { T_NOVEL, T_NOTE, T_JOURNAL, T_OUTLINE, T_TODO, T_PLAIN, T_COUNT };

struct DocFormat {
  char face[24];       // font family name
  uint8_t size;        // 0..5
  uint8_t indentChars; // first-line indent in "n" widths, body paragraphs only
  uint8_t lineSpacing; // percent of the font's line height (100..200)
  uint8_t tight;       // 1 = blank lines collapse to nothing (novel style)
  uint8_t blankLines;  // 1 = Enter inserts a blank line between paragraphs
  uint8_t h1Center;
  uint8_t marginX;     // px each side
  uint8_t pad;
};

// The BLE stack stores 3 bonds by default (CONFIG_BT_NIMBLE_MAX_BONDS), so keep 3 keyboards.
#define KB_SLOTS 3
struct KbSaved {
  char addr[18];
  uint8_t type;
  char name[24];
};

struct Settings {
  uint32_t version;
  DocFormat fmt[T_COUNT];
  uint8_t inverted;
  uint8_t showStatus;
  uint8_t typewriter;  // keep the cursor line near the middle of the screen
  uint8_t autoSync;
  uint16_t autosaveSec;
  uint16_t sleepMin;   // idle minutes before deep sleep (0 = never)
  uint16_t goalWords;  // session goal, 0 = none
  uint16_t autoSyncMin;
  char wifiSsid[33];
  char wifiPass[65];
  char legacyKbAddr[18];  // pre-multi-keyboard; migrated into kbs[0] on load
  uint8_t legacyKbType;
  char davUrl[128];
  char davUser[40];
  char davPass[64];
  char syncRoot[48];   // folder on the card that is synced ("/" = everything)
  char tz[48];         // POSIX TZ string
  char ntp[32];
  // appended later: older saved settings are a prefix of this struct
  KbSaved kbs[KB_SLOTS];  // paired keyboards, most recently used first
};

extern Settings S;

void settingsDefaults();
void settingsLoad();
void settingsSave();
const char *typeName(DocType t);
const char *typeBlurb(DocType t);
