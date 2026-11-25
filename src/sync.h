#pragma once
#include <Arduino.h>

// Two-way WebDAV sync of a folder on the card with a server folder (which can
// itself be a Syncthing folder on a PC / Pi / NAS). Conflicts keep both copies.
namespace davsync {

struct Result {
  bool ok = false;
  int up = 0, down = 0, removed = 0, conflicts = 0;
  String error;
};

bool configured();
Result run(void (*progress)(const String &line));

}  // namespace davsync
