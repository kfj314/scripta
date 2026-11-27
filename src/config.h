#pragma once
#include <Arduino.h>

#define SCRIPTA_VERSION "0.1.0"

// Board-specific pins and panel size: boards/<name>/board.h, put on the
// include path by the board's environment in platformio.ini.
#include "board.h"

// Where scripta keeps its own bookkeeping on the card
#define META_DIR "/.scripta"
#define FONT_DIR "/fonts"
