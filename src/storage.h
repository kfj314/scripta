#pragma once
#include <Arduino.h>
#include <FS.h>
#include <string>
#include <vector>

// File access. Uses the microSD card (SD_MMC, 1-bit) and falls back to the
// internal LittleFS partition so the device still works without a card.
namespace storage {

struct Entry {
  String name;
  bool dir;
  uint32_t size;
  time_t mtime;
};

bool begin();
bool ready();
bool usingSD();
fs::FS &fs();

bool exists(const String &path);
bool isDir(const String &path);
bool readFile(const String &path, std::string &out);
// Writes <path>.tmp, then swaps it in. Safe against power loss mid-write.
bool writeFile(const String &path, const char *data, size_t len);
// Finishes or discards a half-written <path>.tmp left by a power cut.
void recoverTmp(const String &path);
bool mkdirs(const String &path);
bool list(const String &dir, std::vector<Entry> &out);
bool removeAll(const String &path);
bool rename(const String &from, const String &to);
uint32_t crc32(const String &path);
uint32_t crc32(const char *data, size_t len, uint32_t seed = 0);
uint64_t freeBytes();

String join(const String &a, const String &b);
String parent(const String &path);
String baseName(const String &path);
// "a.md" -> "a.md" ; picks "a 2.md" etc. if it already exists.
String uniquePath(const String &path);

}  // namespace storage
