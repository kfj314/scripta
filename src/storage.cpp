#include "storage.h"
#include <LittleFS.h>
#include <SD_MMC.h>
#include <esp_crc.h>
#include <algorithm>
#include "config.h"

namespace storage {

static bool sdOk = false;
static bool lfsOk = false;

bool begin() {
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
  sdOk = SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, 8);
  if (!sdOk) lfsOk = LittleFS.begin(true);
  if (ready()) {
    mkdirs(META_DIR);
  }
  return ready();
}
bool ready() { return sdOk || lfsOk; }
bool usingSD() { return sdOk; }
fs::FS &fs() {
  if (sdOk) return SD_MMC;
  return LittleFS;
}

bool exists(const String &path) { return fs().exists(path); }
bool isDir(const String &path) {
  fs::File f = fs().open(path);
  if (!f) return false;
  bool d = f.isDirectory();
  f.close();
  return d;
}

bool readFile(const String &path, std::string &out) {
  fs::File f = fs().open(path, "r");
  if (!f || f.isDirectory()) return false;
  size_t n = f.size();
  out.clear();
  out.reserve(n + 4096);
  uint8_t buf[2048];
  while (true) {
    int r = f.read(buf, sizeof(buf));
    if (r <= 0) break;
    out.append((const char *)buf, r);
  }
  f.close();
  return true;
}

bool writeFile(const String &path, const char *data, size_t len) {
  String tmp = path + ".tmp";
  fs::File f = fs().open(tmp, "w");
  if (!f) return false;
  size_t done = 0;
  while (done < len) {
    size_t chunk = min((size_t)4096, len - done);
    if (f.write((const uint8_t *)data + done, chunk) != chunk) {
      f.close();
      fs().remove(tmp);
      return false;
    }
    done += chunk;
  }
  f.close();
  if (fs().exists(path)) fs().remove(path);
  return fs().rename(tmp, path);
}

void recoverTmp(const String &path) {
  String tmp = path + ".tmp";
  if (!fs().exists(tmp)) return;
  if (fs().exists(path)) fs().remove(tmp);  // the swap never started
  else fs().rename(tmp, path);              // died between remove and rename
}

bool mkdirs(const String &path) {
  if (path.length() <= 1) return true;
  if (fs().exists(path)) return true;
  int slash = path.lastIndexOf('/');
  if (slash > 0) mkdirs(path.substring(0, slash));
  return fs().mkdir(path);
}

static bool hidden(const String &n) {
  return n.startsWith(".") || n == "System Volume Information" || n.endsWith(".tmp");
}

bool list(const String &dir, std::vector<Entry> &out) {
  out.clear();
  fs::File d = fs().open(dir.length() ? dir : String("/"));
  if (!d || !d.isDirectory()) return false;
  for (fs::File e = d.openNextFile(); e; e = d.openNextFile()) {
    Entry en;
    en.name = e.name();
    if (hidden(en.name)) continue;
    en.dir = e.isDirectory();
    en.size = en.dir ? 0 : e.size();
    en.mtime = e.getLastWrite();
    out.push_back(en);
  }
  std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) {
    if (a.dir != b.dir) return a.dir;
    return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
  });
  return true;
}

bool removeAll(const String &path) {
  if (isDir(path)) {
    std::vector<Entry> items;
    fs::File d = fs().open(path);
    std::vector<String> names;
    if (d) {
      for (fs::File e = d.openNextFile(); e; e = d.openNextFile()) names.push_back(e.name());
    }
    for (auto &n : names) removeAll(join(path, n));
    return fs().rmdir(path);
  }
  return fs().remove(path);
}

bool rename(const String &from, const String &to) { return fs().rename(from, to); }

uint32_t crc32(const char *data, size_t len, uint32_t seed) { return esp_crc32_le(seed, (const uint8_t *)data, len); }

uint32_t crc32(const String &path) {
  fs::File f = fs().open(path, "r");
  if (!f) return 0;
  uint8_t buf[2048];
  uint32_t c = 0;
  while (true) {
    int r = f.read(buf, sizeof(buf));
    if (r <= 0) break;
    c = esp_crc32_le(c, buf, r);
  }
  f.close();
  return c;
}

uint64_t freeBytes() {
  if (sdOk) return SD_MMC.totalBytes() - SD_MMC.usedBytes();
  return LittleFS.totalBytes() - LittleFS.usedBytes();
}

String join(const String &a, const String &b) {
  if (a.endsWith("/")) return a + b;
  return a + "/" + b;
}
String parent(const String &path) {
  int i = path.lastIndexOf('/');
  if (i <= 0) return "/";
  return path.substring(0, i);
}
String baseName(const String &path) {
  int i = path.lastIndexOf('/');
  return i < 0 ? path : path.substring(i + 1);
}
String uniquePath(const String &path) {
  if (!exists(path)) return path;
  int dot = path.lastIndexOf('.');
  int slash = path.lastIndexOf('/');
  String stem = (dot > slash) ? path.substring(0, dot) : path;
  String ext = (dot > slash) ? path.substring(dot) : String("");
  for (int i = 2; i < 1000; i++) {
    String p = stem + " " + String(i) + ext;
    if (!exists(p)) return p;
  }
  return path;
}

}  // namespace storage
