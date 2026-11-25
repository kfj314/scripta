#pragma once
#include <Arduino.h>
#include <vector>
#include "settings.h"

// Per-file bookkeeping kept on the card under /.scripta: which document type
// a file was created as, plus the recently opened list with cursor positions.
namespace meta {

struct Recent {
  String path;
  uint32_t pos;
};

void load();
DocType typeOf(const String &path);
void setType(const String &path, DocType t);
void moved(const String &from, const String &to);
void removed(const String &path);

const std::vector<Recent> &recents();
void touchRecent(const String &path, uint32_t pos);
void savePos(const String &path, uint32_t pos);
uint32_t lastPos(const String &path);

}  // namespace meta
