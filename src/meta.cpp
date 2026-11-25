#include "meta.h"
#include <map>
#include "config.h"
#include "storage.h"

namespace meta {

static std::map<String, uint8_t> types;
static std::vector<Recent> recent;
static const char *kTypes = META_DIR "/types.tsv";
static const char *kRecent = META_DIR "/recent.tsv";
static const size_t kMaxRecent = 12;

static void saveTypes() {
  std::string out;
  for (auto &kv : types) {
    out += kv.first.c_str();
    out += '\t';
    out += std::to_string(kv.second);
    out += '\n';
  }
  storage::writeFile(kTypes, out.data(), out.size());
}

static void saveRecent() {
  std::string out;
  for (auto &r : recent) {
    out += r.path.c_str();
    out += '\t';
    out += std::to_string(r.pos);
    out += '\n';
  }
  storage::writeFile(kRecent, out.data(), out.size());
}

static void parseLines(const std::string &data, void (*fn)(const String &, const String &)) {
  size_t i = 0;
  while (i < data.size()) {
    size_t e = data.find('\n', i);
    if (e == std::string::npos) e = data.size();
    std::string line = data.substr(i, e - i);
    size_t t = line.find('\t');
    if (t != std::string::npos) fn(String(line.substr(0, t).c_str()), String(line.substr(t + 1).c_str()));
    i = e + 1;
  }
}

void load() {
  types.clear();
  recent.clear();
  std::string d;
  if (storage::readFile(kTypes, d))
    parseLines(d, [](const String &a, const String &b) { types[a] = (uint8_t)b.toInt(); });
  d.clear();
  if (storage::readFile(kRecent, d))
    parseLines(d, [](const String &a, const String &b) {
      if (storage::exists(a)) recent.push_back({a, (uint32_t)b.toInt()});
    });
}

DocType typeOf(const String &path) {
  auto it = types.find(path);
  if (it != types.end() && it->second < T_COUNT) return (DocType)it->second;
  String l = path;
  l.toLowerCase();
  return l.endsWith(".txt") ? T_PLAIN : T_NOTE;
}

void setType(const String &path, DocType t) {
  types[path] = t;
  saveTypes();
}

void moved(const String &from, const String &to) {
  bool ch = false;
  auto it = types.find(from);
  if (it != types.end()) {
    types[to] = it->second;
    types.erase(from);
    ch = true;
  }
  // folders: move everything beneath
  std::vector<std::pair<String, uint8_t>> sub;
  String pre = from + "/";
  for (auto &kv : types)
    if (kv.first.startsWith(pre)) sub.push_back({kv.first, kv.second});
  for (auto &s : sub) {
    types.erase(s.first);
    types[to + s.first.substring(from.length())] = s.second;
    ch = true;
  }
  if (ch) saveTypes();
  bool rc = false;
  for (auto &r : recent) {
    if (r.path == from) { r.path = to; rc = true; }
    else if (r.path.startsWith(pre)) { r.path = to + r.path.substring(from.length()); rc = true; }
  }
  if (rc) saveRecent();
}

void removed(const String &path) {
  String pre = path + "/";
  bool ch = types.erase(path) > 0;
  for (auto it = types.begin(); it != types.end();) {
    if (it->first.startsWith(pre)) { it = types.erase(it); ch = true; }
    else ++it;
  }
  if (ch) saveTypes();
  bool rc = false;
  for (size_t i = 0; i < recent.size();) {
    if (recent[i].path == path || recent[i].path.startsWith(pre)) { recent.erase(recent.begin() + i); rc = true; }
    else i++;
  }
  if (rc) saveRecent();
}

const std::vector<Recent> &recents() { return recent; }

void touchRecent(const String &path, uint32_t pos) {
  for (size_t i = 0; i < recent.size(); i++)
    if (recent[i].path == path) { recent.erase(recent.begin() + i); break; }
  recent.insert(recent.begin(), {path, pos});
  if (recent.size() > kMaxRecent) recent.resize(kMaxRecent);
  saveRecent();
}

void savePos(const String &path, uint32_t pos) {
  for (auto &r : recent)
    if (r.path == path) {
      if (r.pos != pos) { r.pos = pos; saveRecent(); }
      return;
    }
}

uint32_t lastPos(const String &path) {
  for (auto &r : recent)
    if (r.path == path) return r.pos;
  return 0;
}

}  // namespace meta
