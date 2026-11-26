#include "app.h"
#include <esp_sleep.h>
#include <algorithm>
#include <driver/rtc_io.h>
#include "config.h"
#include "doc.h"
#include "fonts.h"
#include "gfx.h"
#include "keyboard.h"
#include "meta.h"
#include "net.h"
#include "rtc.h"
#include "settings.h"
#include "storage.h"
#include "sync.h"
#include "ui.h"

using ui::Frame;
using ui::Item;

namespace app {

static Doc doc;
static String curDir = "/";
static uint32_t lastInput = 0;
static uint32_t lastSaveMs = 0;
static uint32_t lastSyncMs = 0;
static int sessionStartWords = 0;
static String toastMsg;
static uint32_t toastUntil = 0;
static bool redraw = true;
static bool sleeping = false;
static int lastMinute = -1;
static uint32_t lastBatMs = 0;
static uint32_t lastStatusMs = 0;
static int batPct = 100;

static void toast(const String &s, uint32_t ms = 2200) {
  toastMsg = s;
  toastUntil = millis() + ms;
  redraw = true;
}

// ------------------------------------------------------------------ helpers

// Single-cell Li-ion resting-voltage curve (mV -> %), interpolated.
static int percentFromMv(int mv) {
  static const int curve[][2] = {{3300, 0}, {3500, 5}, {3600, 12}, {3700, 25}, {3750, 38}, {3800, 50},
                                 {3870, 62}, {3950, 75}, {4050, 88}, {4150, 97}, {4200, 100}};
  const int n = sizeof(curve) / sizeof(curve[0]);
  if (mv <= curve[0][0]) return 0;
  if (mv >= curve[n - 1][0]) return 100;
  for (int i = 1; i < n; i++) {
    if (mv <= curve[i][0]) {
      int span = curve[i][0] - curve[i - 1][0];
      return curve[i - 1][1] + (curve[i][1] - curve[i - 1][1]) * (mv - curve[i - 1][0]) / span;
    }
  }
  return 100;
}

static int batMv = 0;

// Averages several ADC reads (divider is 3:1) and smooths over time.
static int readBattery() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(PIN_BAT);
  int mv = (int)(sum / 16) * 3;
  batMv = batMv ? (batMv * 3 + mv) / 4 : mv;
  return percentFromMv(batMv);
}

static String twoDigits(int v) { return v < 10 ? "0" + String(v) : String(v); }

static String clockText() {
  struct tm t;
  if (!rtc::localTime(t)) return "";
  return twoDigits(t.tm_hour) + ":" + twoDigits(t.tm_min);
}

static const char *kHelp[][2] = {
    {"Esc", "Menu (new, open, format, Wi-Fi, ...)"},
    {"Ctrl+S", "Save (also autosaves)"},
    {"Ctrl+O / Ctrl+N", "Open files / new file"},
    {"Ctrl+Z / Ctrl+Y", "Undo / redo"},
    {"Ctrl+B / Ctrl+I", "Bold **x** / italic _x_"},
    {"Ctrl+1 2 3 / 0", "Heading 1-3 / plain paragraph"},
    {"Ctrl+L / Ctrl+K", "Bullet list / numbered list"},
    {"Ctrl+Enter", "Tick a to-do checkbox"},
    {"Tab / Shift+Tab", "Indent / outdent list item"},
    {"Ctrl+= / Ctrl+-", "Bigger / smaller text"},
    {"Ctrl+F", "Next font"},
    {"Ctrl+D", "Flip light / dark"},
    {"Ctrl+G", "Set word goal"},
    {"Ctrl+R or F5", "Sync now"},
    {"Ctrl+Left/Right", "Jump by word"},
    {"Ctrl+Home/End", "Start / end of document"},
    {"Ctrl+Backspace", "Delete previous word"},
    {"Ctrl+.", "Show / hide status bar"},
    {"Ctrl+Q", "Sleep"},
    {"Hold KEY button", "Sleep (wake with KEY)"},
    {"Hold BOOT + KEY", "Bluetooth: scan for keyboards"},
};

// ------------------------------------------------------------------ documents

static void saveDoc(bool quiet = true) {
  if (!doc.open()) return;
  if (doc.dirty()) {
    if (doc.save()) {
      lastSaveMs = millis();
      if (!quiet) toast("Saved");
    } else {
      toast("Save FAILED - check card");
    }
  } else if (!quiet) {
    toast("Saved");
  }
  meta::savePos(doc.path(), doc.cursor());
}

static void openDoc(const String &path) {
  if (doc.open()) saveDoc();
  DocType t = meta::typeOf(path);
  if (!doc.load(path, t)) {
    toast("Could not open file");
    return;
  }
  uint32_t pos = meta::lastPos(path);
  doc.setCursor(pos);
  meta::touchRecent(path, pos);
  curDir = storage::parent(path);
  sessionStartWords = doc.wordsAtOpen();
  ui::popAll();
  redraw = true;
}

static String sanitize(String n) {
  const char *bad = "/\\:*?\"<>|";
  for (size_t i = 0; i < n.length(); i++)
    if (strchr(bad, n[i])) n.setCharAt(i, '-');
  n.trim();
  return n;
}

static void createFile(DocType t, String name) {
  name = sanitize(name);
  if (name.isEmpty()) name = "Untitled";
  String lower = name;
  lower.toLowerCase();
  bool md = true;
  String fn = name;
  if (lower.endsWith(".txt")) md = false;
  else if (!lower.endsWith(".md") && !lower.endsWith(".markdown")) fn = name + (t == T_PLAIN ? ".txt" : ".md");
  if (fn.endsWith(".txt")) md = false;
  String title = name;
  int dot = title.lastIndexOf('.');
  if (dot > 0) title = title.substring(0, dot);
  storage::mkdirs(curDir);
  String path = storage::uniquePath(storage::join(curDir, fn));
  String body;
  if (md) {
    switch (t) {
      case T_NOVEL:
      case T_NOTE: body = "# " + title + "\n\n"; break;
      case T_JOURNAL: body = "# " + rtc::longDate() + "\n\n"; break;
      case T_OUTLINE: body = "# " + title + "\n\n- "; break;
      case T_TODO: body = "# " + title + "\n\n- [ ] "; break;
      default: break;
    }
  }
  if (!storage::writeFile(path, body.c_str(), body.length())) {
    toast("Could not create file");
    return;
  }
  meta::setType(path, t);
  openDoc(path);
  doc.setCursor(doc.size());
}

static String suggestName(DocType t) {
  if (t == T_JOURNAL) return rtc::dateStamp();
  if (t == T_NOVEL) {
    std::vector<storage::Entry> items;
    int n = 0;
    if (storage::list(curDir, items))
      for (auto &e : items) if (!e.dir && e.name.startsWith("Chapter")) n++;
    return "Chapter " + String(n + 1);
  }
  if (t == T_TODO) return "To-do";
  return "Untitled";
}

static void newFileFlow() {
  Frame f;
  f.title = "New file in " + curDir + " - what is it?";
  f.build = [](std::vector<Item> &items) {
    for (int t = 0; t < T_COUNT; t++) {
      DocType dt = (DocType)t;
      items.push_back(ui::item(typeName(dt), typeBlurb(dt), [dt]() {
        ui::askText(String("Name your ") + typeName(dt), suggestName(dt), false, [dt](const String &n) { createFile(dt, n); });
      }));
    }
  };
  ui::push(f);
}

// ------------------------------------------------------------------ file browser

static void browserFrame(String dir);

static void doRename(const String &path, bool isDir) {
  ui::askText(isDir ? "Rename folder" : "Rename file", storage::baseName(path), false, [path](const String &nn) {
    String n = sanitize(nn);
    if (n.isEmpty()) return;
    String to = storage::join(storage::parent(path), n);
    if (to == path) return;
    if (doc.open() && doc.path() == path) saveDoc();
    if (storage::rename(path, to)) {
      meta::moved(path, to);
      if (doc.open() && doc.path() == path) doc.closeDoc();
      ui::refresh();
    } else toast("Rename failed");
  });
}

static void doMove(const String &path) {
  ui::askText("Move to folder (path)", storage::parent(path), false, [path](const String &d) {
    String dest = d;
    if (!dest.startsWith("/")) dest = "/" + dest;
    storage::mkdirs(dest);
    String to = storage::join(dest, storage::baseName(path));
    if (doc.open() && doc.path() == path) saveDoc();
    if (storage::rename(path, to)) {
      meta::moved(path, to);
      if (doc.open() && doc.path() == path) doc.closeDoc();
      ui::refresh();
    } else toast("Move failed");
  });
}

// Keys for the file browser; the row is identified by its label.
static bool browserKey(const Key &k, const String &label, bool isUpRow) {
  if (isUpRow || label.startsWith("(")) return false;
  bool isDir = label.endsWith("/");
  String name = isDir ? label.substring(0, label.length() - 1) : label;
  String full = storage::join(curDir, name);
  if (k.code == K_DELETE) {
    ui::askYesNo("Delete " + name + (isDir ? " and its contents?" : "?"), [full, name]() {
      if (doc.open() && (doc.path() == full || doc.path().startsWith(full + "/"))) doc.closeDoc();
      storage::removeAll(full);
      meta::removed(full);
      ui::refresh();
      toast("Deleted " + name);
    });
    return true;
  }
  if (k.code == 'r' || k.code == K_F2) { doRename(full, isDir); return true; }
  if (k.code == 'm') { doMove(full); return true; }
  return false;
}

static void browserFrame(String dir) {
  curDir = dir;
  Frame f;
  f.title = dir;
  f.hint = "Enter open  Bksp up  N new  F folder  R rename  M move  Del delete";
  f.build = [](std::vector<Item> &items) {
    if (curDir != "/")
      items.push_back(ui::item(".. (up)", "", []() {
        String up = storage::parent(curDir);
        ui::pop();
        ui::defer([up]() { browserFrame(up); });
      }));
    std::vector<storage::Entry> list;
    storage::list(curDir, list);
    for (auto &e : list) {
      String full = storage::join(curDir, e.name);
      if (e.dir) {
        items.push_back(ui::item(e.name + "/", "", [full]() {
          ui::pop();
          ui::defer([full]() { browserFrame(full); });
        }));
      } else {
        String d = String(typeName(meta::typeOf(full)));
        String lower = e.name;
        lower.toLowerCase();
        bool editable = lower.endsWith(".md") || lower.endsWith(".txt") || lower.endsWith(".markdown");
        Item it = ui::item(e.name, editable ? d : String("(not text)"), [full, editable]() {
          if (editable) openDoc(full);
          else toast("Only .md and .txt open here");
        });
        it.dim = !editable;
        items.push_back(it);
      }
    }
    if (list.empty()) {
      Item e = ui::item("(empty folder)", "", nullptr);
      e.dim = true;
      items.push_back(e);
    }
  };
  f.onKey = [](const Key &k) -> bool {
    if (k.code == 'n') { newFileFlow(); return true; }
    if (k.code == 'f') {
      ui::askText("New folder name", "", false, [](const String &n) {
        String s = sanitize(n);
        if (s.length()) storage::mkdirs(storage::join(curDir, s));
        ui::refresh();
      });
      return true;
    }
    if (k.code == K_BACKSPACE || k.code == K_LEFT) {
      if (curDir != "/") {
        String up = storage::parent(curDir);
        ui::pop();
        ui::defer([up]() { browserFrame(up); });
      } else {
        ui::pop();
      }
      return true;
    }
    String lab = ui::selectedLabel();
    return browserKey(k, lab, lab.startsWith(".. "));
  };
  ui::push(f);
}

// ------------------------------------------------------------------ settings

static const int kSizePt[fonts::LEVELS] = {8, 10, 12, 14, 18, 24};

static void applyFormat() {
  settingsSave();
  doc.relayout();
}

static void formatFrame(DocType t) {
  Frame f;
  f.title = String("Formatting: ") + typeName(t);
  f.build = [t](std::vector<Item> &items) {
    DocFormat &d = S.fmt[t];
    items.push_back(ui::choiceItem("Font", d.face, [t](int dir) {
      int n = fonts::faceCount();
      int cur = fonts::faceIndex(S.fmt[t].face);
      cur = (cur + dir + n) % n;
      strlcpy(S.fmt[t].face, fonts::faceName(cur), sizeof(S.fmt[t].face));
      applyFormat();
    }));
    items.push_back(ui::choiceItem("Size", String(kSizePt[d.size]) + " pt", [t](int dir) {
      S.fmt[t].size = constrain((int)S.fmt[t].size + dir, 0, fonts::LEVELS - 1);
      applyFormat();
    }));
    items.push_back(ui::choiceItem("Line spacing", String(d.lineSpacing) + "%", [t](int dir) {
      S.fmt[t].lineSpacing = constrain((int)S.fmt[t].lineSpacing + dir * 5, 100, 200);
      applyFormat();
    }));
    items.push_back(ui::choiceItem("First-line indent", d.indentChars ? String(d.indentChars) + " chars" : String("none"), [t](int dir) {
      S.fmt[t].indentChars = constrain((int)S.fmt[t].indentChars + dir, 0, 6);
      applyFormat();
    }));
    items.push_back(ui::toggleItem("Enter adds a blank line", d.blankLines, [t](bool v) { S.fmt[t].blankLines = v; applyFormat(); }));
    items.push_back(ui::toggleItem("Hide blank lines (tight)", d.tight, [t](bool v) { S.fmt[t].tight = v; applyFormat(); }));
    items.push_back(ui::toggleItem("Center chapter titles (H1)", d.h1Center, [t](bool v) { S.fmt[t].h1Center = v; applyFormat(); }));
    items.push_back(ui::choiceItem("Side margins", String(d.marginX) + " px", [t](int dir) {
      S.fmt[t].marginX = constrain((int)S.fmt[t].marginX + dir * 2, 0, 80);
      applyFormat();
    }));
    items.push_back(ui::item("Reset to default", "", [t]() {
      ui::askYesNo("Reset this preset?", [t]() {
        Settings d;
        Settings keep = S;
        settingsDefaults();
        d = S;
        S = keep;
        S.fmt[t] = d.fmt[t];
        applyFormat();
        ui::refresh();
      });
    }));
  };
  ui::push(f);
}

static void setClockFlow() {
  struct tm t;
  String init = "2026-01-01 12:00";
  if (rtc::localTime(t)) {
    char b[24];
    strftime(b, sizeof(b), "%Y-%m-%d %H:%M", &t);
    init = b;
  }
  ui::askText("Set clock: YYYY-MM-DD HH:MM", init, false, [](const String &v) {
    int y, mo, d, h, mi;
    if (sscanf(v.c_str(), "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) != 5) { toast("Bad format"); return; }
    struct tm tt = {};
    tt.tm_year = y - 1900;
    tt.tm_mon = mo - 1;
    tt.tm_mday = d;
    tt.tm_hour = h;
    tt.tm_min = mi;
    rtc::setLocal(tt);
    toast("Clock set");
  });
}

static void aboutFrame() {
  Frame f;
  f.title = "About & memory";
  f.build = [](std::vector<Item> &items) {
    auto line = [&](const String &a, const String &b, ui::Icon ic = ui::I_NONE) { items.push_back(ui::item(a, b, nullptr, ic)); };
    auto kb_ = [](uint32_t v) { return String((unsigned long)(v / 1024)) + " KB"; };
    items.push_back(ui::section("Device"));
    line("Scripta", SCRIPTA_VERSION, ui::I_INFO);
    line("Storage", storage::usingSD() ? "microSD" : (storage::ready() ? "internal flash (no card!)" : "NONE"), ui::I_FOLDER);
    if (storage::ready()) line("Free space", String((unsigned long)(storage::freeBytes() / 1024 / 1024)) + " MB");
    line("Battery", String(batPct) + "%  " + String(batMv) + " mV", ui::I_BATT);
    line("Bluetooth", kb::stateText(), ui::I_BT);
    items.push_back(ui::section("Memory"));
    line("Internal RAM free", kb_(ESP.getFreeHeap()) + " of " + kb_(ESP.getHeapSize()));
    line("Largest free block", kb_(ESP.getMaxAllocHeap()));
    line("PSRAM free", ESP.getPsramSize() ? kb_(ESP.getFreePsram()) + " of " + kb_(ESP.getPsramSize()) : String("not detected"));
    line("Firmware", kb_(ESP.getSketchSize()) + " of " + kb_(ESP.getSketchSize() + ESP.getFreeSketchSpace()));
    line("Flash chip", String((unsigned long)(ESP.getFlashChipSize() / 1024 / 1024)) + " MB");
    if (doc.open()) {
      items.push_back(ui::section("Open document"));
      line("Size", kb_(doc.size()) + "  (" + String(doc.words()) + " words)", ui::I_FILE);
    }
    items.push_back(ui::section("Fonts"));
    line("Faces available", String(fonts::faceCount()), ui::I_FONT);
  };
  ui::push(f);
}

static void settingsFrame() {
  Frame f;
  f.title = "Settings";
  f.build = [](std::vector<Item> &items) {
    items.push_back(ui::section("Display"));
    items.push_back(ui::toggleItem("Dark theme (light text)", S.inverted, [](bool v) {
      S.inverted = v;
      gfx::setInverted(v);
      settingsSave();
    }, ui::I_GEAR));
    items.push_back(ui::toggleItem("Status bar while writing", S.showStatus, [](bool v) { S.showStatus = v; settingsSave(); }, ui::I_INFO));
    items.push_back(ui::toggleItem("Typewriter scrolling", S.typewriter, [](bool v) { S.typewriter = v; settingsSave(); }, ui::I_TEXT));
    static const char *kFront[] = {"show", "hide, open below", "hide + lock"};
    items.push_back(ui::choiceItem("YAML properties (.md)", kFront[min<int>(S.hideFront, 2)], [](int d) {
      S.hideFront = ((int)S.hideFront + d + 3) % 3;
      settingsSave();
      if (doc.open()) doc.setCursor(doc.cursor());  // step out of front matter being hidden
      doc.relayout();
    }, ui::I_FILE));
    items.push_back(ui::section("Writing"));
    items.push_back(ui::choiceItem("Autosave after", String(S.autosaveSec) + " s", [](int d) {
      S.autosaveSec = constrain((int)S.autosaveSec + d * 5, 5, 120);
      settingsSave();
    }, ui::I_CHECK));
    items.push_back(ui::choiceItem("Word goal", S.goalWords ? String(S.goalWords) : String("none"), [](int d) {
      S.goalWords = constrain((int)S.goalWords + d * 100, 0, 20000);
      settingsSave();
    }, ui::I_GOAL));
    items.push_back(ui::choiceItem("Sleep after idle", S.sleepMin ? String(S.sleepMin) + " min" : String("never"), [](int d) {
      S.sleepMin = constrain((int)S.sleepMin + d * 5, 0, 120);
      settingsSave();
    }, ui::I_SLEEP));
    items.push_back(ui::section("Formatting by document type"));
    for (int t = 0; t < T_COUNT; t++) {
      DocType dt = (DocType)t;
      items.push_back(ui::item(typeName(dt), S.fmt[t].face, [dt]() { formatFrame(dt); }, ui::I_FONT));
    }
    items.push_back(ui::item("Rescan fonts on card", String(fonts::faceCount() - 3) + " extra", []() {
      fonts::rescan();
      doc.relayout();
      ui::refresh();
    }, ui::I_SYNC));
    items.push_back(ui::section("Clock"));
    items.push_back(ui::item("Set clock...", clockText(), []() { setClockFlow(); }, ui::I_CLOCK));
    items.push_back(ui::item("Time zone (POSIX)", S.tz, []() {
      ui::askText("Time zone, e.g. EST5EDT,M3.2.0,M11.1.0", S.tz, false, [](const String &v) {
        strlcpy(S.tz, v.c_str(), sizeof(S.tz));
        settingsSave();
        rtc::applyTz();
        ui::refresh();
      });
    }, ui::I_CLOCK));
    items.push_back(ui::section("System"));
    items.push_back(ui::item("About & memory", "", []() { aboutFrame(); }, ui::I_INFO));
  };
  f.onClose = []() { settingsSave(); doc.relayout(); };
  ui::push(f);
}

static void helpFrame() {
  Frame f;
  f.title = "Keyboard shortcuts";
  f.hint = "Up/Down scroll   Esc back";
  f.build = [](std::vector<Item> &items) {
    for (auto &h : kHelp) items.push_back(ui::item(h[0], h[1], nullptr));
  };
  ui::push(f);
}

// ------------------------------------------------------------------ wifi + sync

static std::vector<String> syncLog;

static void syncProgress(const String &line) {
  syncLog.push_back(line);
  gfx::clear();
  ui::busy("Syncing", syncLog);
}

static void runSync() {
  if (!davsync::configured()) {
    toast("Set up a sync server first (Wi-Fi menu)", 3000);
    return;
  }
  saveDoc();
  syncLog.clear();
  ui::busy("Syncing", {"Connecting to Wi-Fi..."});
  bool wasWeb = web::running();
  if (!net::connectSaved(15000)) {
    toast("Wi-Fi not connected", 3000);
    return;
  }
  davsync::Result r = davsync::run(syncProgress);
  if (doc.open() && !doc.dirty() && (r.down > 0 || r.conflicts > 0)) doc.reload();
  lastSyncMs = millis();
  if (!wasWeb) net::off();
  if (r.ok) {
    String s = "Synced: " + String(r.up) + " up, " + String(r.down) + " down";
    if (r.conflicts) s += ", " + String(r.conflicts) + " conflict(s)";
    toast(s, 3500);
  } else {
    ui::message("Sync failed", r.error);
  }
  redraw = true;
}

static void networkPicker() {
  ui::busy("Wi-Fi", {"Scanning..."});
  std::vector<net::Network> nets = net::scan();
  Frame f;
  f.title = "Choose a network";
  f.build = [nets](std::vector<Item> &items) {
    for (auto &n : nets) {
      String ssid = n.ssid;
      bool secure = n.secure;
      items.push_back(ui::item(ssid, String(secure ? "locked " : "open ") + String(n.rssi) + " dBm", [ssid, secure]() {
        auto go = [ssid](const String &pass) {
          ui::busy("Wi-Fi", {"Connecting to " + ssid + "..."});
          if (net::connect(ssid, pass, 15000)) {
            strlcpy(S.wifiSsid, ssid.c_str(), sizeof(S.wifiSsid));
            strlcpy(S.wifiPass, pass.c_str(), sizeof(S.wifiPass));
            settingsSave();
            toast("Connected: " + net::ip(), 3000);
            ui::pop();
          } else {
            toast("Could not connect", 3000);
          }
        };
        if (secure) ui::askText("Password for " + ssid, ssid == S.wifiSsid ? String(S.wifiPass) : String(""), true, go);
        else go("");
      }));
    }
    if (nets.empty()) items.push_back(ui::item("No networks found", "", nullptr));
  };
  ui::push(f);
}

static void hotspotFrame() {
  if (!net::startAP()) { toast("Could not start hotspot"); return; }
  web::start();
  Frame f;
  f.title = "Phone setup hotspot";
  f.hint = "Esc stops the hotspot";
  f.build = [](std::vector<Item> &items) {
    items.push_back(ui::item("1. On your phone join Wi-Fi", net::apName(), nullptr));
    items.push_back(ui::item("   password", net::apPass(), nullptr));
    items.push_back(ui::item("2. Open", "http://" + net::ip(), nullptr));
    items.push_back(ui::item("3. Fill in Wi-Fi + sync, Save", "", nullptr));
  };
  f.onClose = []() {
    web::stop();
    net::stopAP();
    ui::message("Hotspot off", "Use Wi-Fi > Connect to join your network.");
  };
  ui::push(f);
}

static void transferFrame() {
  if (!net::connectSaved(15000)) { toast("Connect to Wi-Fi first", 3000); return; }
  web::start();
  Frame f;
  f.title = "Wi-Fi file transfer";
  f.hint = "Esc stops the server";
  f.build = [](std::vector<Item> &items) {
    items.push_back(ui::item("Open in a browser on your network:", "", nullptr));
    items.push_back(ui::item("http://" + net::ip(), "", nullptr));
    items.push_back(ui::item("Upload, download, rename, delete files.", "", nullptr));
  };
  f.onClose = []() {
    web::stop();
    if (doc.open() && !doc.dirty()) doc.reload();
    net::off();
  };
  ui::push(f);
}

static void promptDav(const char *title, char *dst, size_t n, bool mask) {
  ui::askText(title, mask ? String("") : String(dst), mask, [dst, n](const String &v) {
    strlcpy(dst, v.c_str(), n);
    settingsSave();
    ui::refresh();
  });
}

static void wifiFrame() {
  Frame f;
  f.title = "Wi-Fi & sync";
  f.build = [](std::vector<Item> &items) {
    items.push_back(ui::section("Connection"));
    items.push_back(ui::item(net::online() ? "Connected: " + net::ssid() : String("Not connected"), net::online() ? net::ip() : String(S.wifiSsid), []() {
      if (!net::online()) {
        ui::busy("Wi-Fi", {"Connecting..."});
        toast(net::connectSaved(15000) ? "Connected" : "Could not connect", 2500);
        ui::refresh();
      }
    }, ui::I_WIFI));
    items.push_back(ui::item("Connect to a network...", "", []() { networkPicker(); }, ui::I_WIFI));
    items.push_back(ui::item("Setup from phone (hotspot)...", "", []() { hotspotFrame(); }, ui::I_WIFI));
    items.push_back(ui::item("Turn Wi-Fi off", "", []() { net::off(); toast("Wi-Fi off"); ui::refresh(); }, ui::I_CLOSE));
    items.push_back(ui::section("Sync"));
    items.push_back(ui::item("Sync now", davsync::configured() ? "" : "not set up", []() { ui::popAll(); ui::defer([]() { runSync(); }); }, ui::I_SYNC));
    items.push_back(ui::toggleItem("Auto-sync when idle", S.autoSync, [](bool v) { S.autoSync = v; settingsSave(); }, ui::I_SYNC));
    items.push_back(ui::choiceItem("Auto-sync every", String(S.autoSyncMin) + " min", [](int d) {
      S.autoSyncMin = constrain((int)S.autoSyncMin + d * 5, 5, 240);
      settingsSave();
    }, ui::I_CLOCK));
    items.push_back(ui::section("Sync server (WebDAV)"));
    items.push_back(ui::item("Server URL", S.davUrl[0] ? String(S.davUrl) : String("not set"), []() { promptDav("WebDAV URL", S.davUrl, sizeof(S.davUrl), false); }));
    items.push_back(ui::item("User", S.davUser, []() { promptDav("WebDAV user", S.davUser, sizeof(S.davUser), false); }));
    items.push_back(ui::item("Password", S.davPass[0] ? "set" : "", []() { promptDav("WebDAV password", S.davPass, sizeof(S.davPass), true); }));
    items.push_back(ui::item("Folder to sync", S.syncRoot, []() { promptDav("Card folder to sync", S.syncRoot, sizeof(S.syncRoot), false); }, ui::I_FOLDER));
    items.push_back(ui::section("Other"));
    items.push_back(ui::item("Wi-Fi file transfer (browser)...", "", []() { transferFrame(); }, ui::I_FOLDER));
    items.push_back(ui::item("Set time from internet", "", []() {
      ui::busy("Clock", {"Connecting..."});
      if (net::connectSaved(15000)) {
        rtc::syncFromNtp(8000);
        toast("Clock: " + clockText());
        net::off();
      } else toast("Wi-Fi not connected");
    }, ui::I_CLOCK));
  };
  ui::push(f);
}

// ------------------------------------------------------------------ bluetooth

static void btFrame() {
  Frame f;
  f.title = "Bluetooth keyboard";
  f.tickMs = 400;
  f.hint = "Pairing mode, then Scan. BOOT=down KEY=select. Shortcut: hold BOOT+KEY";
  f.onTick = []() { ui::refresh(); };
  f.build = [](std::vector<Item> &items) {
    if (kb::passkey()) {
      char b[8];
      snprintf(b, sizeof(b), "%06u", (unsigned)kb::passkey());
      items.push_back(ui::item(String("Type ") + b + " on the keyboard, then Enter", "", nullptr, ui::I_INFO));
    }
    items.push_back(ui::section("Status"));
    items.push_back(ui::item(kb::stateText(), "", []() {
      if (kb::savedCount() && !kb::connected()) kb::reconnectSaved();
    }, ui::I_BT));
    if (kb::savedCount()) {
      items.push_back(ui::section("Saved keyboards"));
      for (int i = 0; i < kb::savedCount(); i++) {
        String addr = S.kbs[i].addr;
        uint8_t type = S.kbs[i].type;
        String name = S.kbs[i].name[0] ? String(S.kbs[i].name) : addr;
        bool on = kb::isConnectedTo(addr.c_str());
        items.push_back(ui::item(name, on ? String("connected") : addr.substring(addr.length() - 5), [addr, type, on]() {
          if (!on) kb::connectTo(addr, type);
          ui::refresh();
        }, on ? ui::I_CHECK : ui::I_BT));
      }
    }
    items.push_back(ui::section("Find a keyboard"));
    items.push_back(ui::item(kb::scanning() ? "Scanning..." : "Scan for keyboards", "", []() { kb::startScan(); ui::refresh(); }, ui::I_SYNC));
    auto found = kb::found();
    std::sort(found.begin(), found.end(), [](const kb::Found &a, const kb::Found &b) {
      if (a.hid != b.hid) return a.hid;
      return a.rssi > b.rssi;
    });
    for (auto &d : found) {
      String addr = d.addr;
      uint8_t type = d.type;
      items.push_back(ui::item(d.name, String(d.hid ? "HID " : "") + addr.substring(addr.length() - 5) + " " + String(d.rssi), [addr, type]() {
        kb::connectTo(addr, type);
        ui::refresh();
      }, d.hid ? ui::I_BT : ui::I_NONE));
    }
    if (kb::savedCount()) {
      items.push_back(ui::section("Manage"));
      for (int i = 0; i < kb::savedCount(); i++) {
        String addr = S.kbs[i].addr;
        String name = S.kbs[i].name[0] ? String(S.kbs[i].name) : addr;
        items.push_back(ui::item(String("Forget ") + name, "", [addr, name]() {
          ui::askYesNo(String("Forget ") + name + "?", [addr]() { kb::forget(addr); toast("Forgotten"); ui::refresh(); });
        }, ui::I_CLOSE));
      }
      if (kb::savedCount() > 1)
        items.push_back(ui::item("Forget all keyboards", "", []() {
          ui::askYesNo("Forget all keyboards?", []() { kb::forgetAll(); toast("Forgotten"); ui::refresh(); });
        }, ui::I_CLOSE));
    }
  };
  ui::push(f);
}

// ------------------------------------------------------------------ menus

static void sleepNow();

static void mainMenu() {
  Frame f;
  f.title = doc.open() ? storage::baseName(doc.path()) : String("Scripta");
  f.build = [](std::vector<Item> &items) {
    if (doc.open()) {
      items.push_back(ui::section("This document"));
      items.push_back(ui::item("Resume writing", "Esc", []() { ui::popAll(); }, ui::I_RESUME));
      items.push_back(ui::choiceItem("Document type", typeName(doc.type()), [](int d) {
        int t = ((int)doc.type() + d + T_COUNT) % T_COUNT;
        doc.setType((DocType)t);
        meta::setType(doc.path(), (DocType)t);
      }, ui::I_TEXT));
      items.push_back(ui::item("Formatting for this type...", "", []() { formatFrame(doc.type()); }, ui::I_FONT));
      items.push_back(ui::choiceItem("Word goal", S.goalWords ? String(S.goalWords) : String("none"), [](int d) {
        S.goalWords = constrain((int)S.goalWords + d * 100, 0, 20000);
        settingsSave();
      }, ui::I_GOAL));
    }
    items.push_back(ui::section("Files"));
    items.push_back(ui::item("New file...", "Ctrl+N", []() { ui::pop(); ui::defer(newFileFlow); }, ui::I_NEW));
    items.push_back(ui::item("Open / manage files...", "Ctrl+O", []() { ui::pop(); ui::defer([]() { browserFrame(curDir); }); }, ui::I_FOLDER));
    items.push_back(ui::section("Connections"));
    items.push_back(ui::item("Bluetooth keyboard...", kb::connected() ? "connected" : "", []() { btFrame(); }, ui::I_BT));
    items.push_back(ui::item("Wi-Fi & sync...", net::online() ? "online" : "", []() { wifiFrame(); }, ui::I_WIFI));
    items.push_back(ui::section("More"));
    items.push_back(ui::item("Settings...", "Ctrl+,", []() { settingsFrame(); }, ui::I_GEAR));
    items.push_back(ui::item("Shortcuts help", "Ctrl+H", []() { helpFrame(); }, ui::I_HELP));
    items.push_back(ui::item("Sleep", "Ctrl+Q", []() { ui::popAll(); ui::defer(sleepNow); }, ui::I_SLEEP));
    if (doc.open())
      items.push_back(ui::item("Close document", "", []() {
        saveDoc();
        doc.closeDoc();
        ui::popAll();
      }, ui::I_CLOSE));
  };
  ui::push(f);
}

// ------------------------------------------------------------------ home

// Focus ids: 0 = hero card, 1..4 = recent files, 100..105 = tool tiles.
static int homeFocus = 0;
static const int kTiles = 6;

static int recentCount() {
  int n = (int)meta::recents().size() - 1;
  return n < 0 ? 0 : (n > 4 ? 4 : n);
}

static void homeActivate() {
  const auto &rec = meta::recents();
  if (homeFocus == 0) {
    if (rec.empty()) newFileFlow();
    else openDoc(rec[0].path);
  } else if (homeFocus < 100) {
    if (homeFocus < (int)rec.size()) openDoc(rec[homeFocus].path);
  } else {
    switch (homeFocus - 100) {
      case 0: newFileFlow(); break;
      case 1: browserFrame(curDir); break;
      case 2: btFrame(); break;
      case 3: wifiFrame(); break;
      case 4: settingsFrame(); break;
      case 5: helpFrame(); break;
    }
  }
}

static bool homeKey(const Key &k) {
  if (k.ctrl() || k.alt()) return false;
  int rc = recentCount();
  bool tile = homeFocus >= 100;
  int t = homeFocus - 100;
  switch (k.code) {
    case K_UP:
      if (!tile) homeFocus = homeFocus > 0 ? homeFocus - 1 : 0;
      else if (t / 2 == 0) homeFocus = 0;
      else homeFocus -= 2;
      return true;
    case K_DOWN:
      if (homeFocus == 0) homeFocus = rc >= 1 ? 1 : 100;
      else if (!tile) homeFocus = homeFocus < rc ? homeFocus + 1 : homeFocus;
      else if (t / 2 < 2) homeFocus += 2;
      return true;
    case K_LEFT:
      if (tile) {
        if (t % 2 == 1) homeFocus--;
        else homeFocus = rc >= 1 ? min(1 + t / 2, rc) : 0;
      }
      return true;
    case K_RIGHT:
      if (homeFocus == 0) return true;
      if (!tile) homeFocus = 100 + 2 * min(homeFocus - 1, 2);
      else if (t % 2 == 0) homeFocus++;
      return true;
    case K_ENTER:
    case ' ':
      homeActivate();
      return true;
    case K_ESC:
      if (doc.open()) ui::pop();
      return true;
    case 'n': newFileFlow(); return true;
    case 'f': browserFrame(curDir); return true;
    case 'b': btFrame(); return true;
    case 'w': wifiFrame(); return true;
    case 's': settingsFrame(); return true;
    case 'h': helpFrame(); return true;
    default: return k.code == K_BACKSPACE;
  }
}

static void homeDraw() {
  U8G2 &u = gfx::u();
  const auto &rec = meta::recents();
  ui::drawHeader("Scripta");

  // hero card
  bool hs = homeFocus == 0;
  if (hs) gfx::fillRect(8, 32, 384, 56, true);
  u.setDrawColor(hs ? gfx::paper() : gfx::ink());
  u.drawRFrame(8, 32, 384, 56, 5);
  ui::drawIcon(rec.empty() ? ui::I_NEW : ui::I_FILE, 32, 60, 24);
  u.setFont(u8g2_font_helvB08_tf);
  u.drawStr(56, 48, rec.empty() ? "START WRITING" : "CONTINUE WRITING");
  u.setFont(u8g2_font_helvB14_tf);
  String name = rec.empty() ? String("Create your first file") : storage::baseName(rec[0].path);
  while (name.length() > 4 && u.getUTF8Width(name.c_str()) > 320) name.remove(name.length() - 1);
  u.drawUTF8(56, 70, name.c_str());
  u.setFont(u8g2_font_helvR08_tf);
  String sub = rec.empty() ? String("Press Enter, or N") : String(typeName(meta::typeOf(rec[0].path))) + "   " + storage::parent(rec[0].path);
  u.drawUTF8(56, 83, sub.c_str());
  u.setDrawColor(gfx::ink());

  // recent column
  u.setFont(u8g2_font_helvB08_tf);
  u.drawStr(10, 106, "RECENT");
  u.drawHLine(52, 102, 148);
  u.drawHLine(52, 103, 148);
  int rc = recentCount();
  u.setFont(u8g2_font_helvR10_tf);
  for (int i = 1; i <= rc; i++) {
    int y = 110 + (i - 1) * 24;
    bool s = homeFocus == i;
    if (s) gfx::fillRect(6, y, 194, 22, true);
    u.setDrawColor(s ? gfx::paper() : gfx::ink());
    ui::drawIcon(ui::I_FILE, 20, y + 11, 14);
    String n = storage::baseName(rec[i].path);
    while (n.length() > 3 && u.getUTF8Width(n.c_str()) > 160) n.remove(n.length() - 1);
    u.drawUTF8(34, y + 15, n.c_str());
    u.setDrawColor(gfx::ink());
  }
  if (rc == 0) {
    u.setFont(u8g2_font_helvR08_tf);
    u.drawStr(12, 126, "No other recent files yet");
  }
  // storage chip
  u.setFont(u8g2_font_helvR08_tf);
  String st = storage::usingSD() ? String("SD card  ") + String((unsigned long)(storage::freeBytes() / 1024 / 1024)) + " MB free"
                                 : String(storage::ready() ? "NO SD CARD - using flash" : "Storage unavailable");
  u.drawRFrame(8, 236, 192, 20, 4);
  ui::drawIcon(ui::I_FOLDER, 20, 246, 12);
  u.drawUTF8(32, 249, st.c_str());
  String kbs = kb::connected() ? "Keyboard connected" : (kb::savedCount() ? "Keyboard paired" : "No keyboard paired");
  u.drawRFrame(8, 260, 192, 20, 4);
  ui::drawIcon(ui::I_BT, 20, 270, 12);
  u.drawUTF8(32, 273, kbs.c_str());

  // tools column
  u.setFont(u8g2_font_helvB08_tf);
  u.drawStr(208, 106, "TOOLS");
  u.drawHLine(244, 102, 148);
  u.drawHLine(244, 103, 148);
  static const char *labels[kTiles] = {"New", "Files", "Bluetooth", "Wi-Fi & sync", "Settings", "Help"};
  static const ui::Icon icons[kTiles] = {ui::I_NEW, ui::I_FOLDER, ui::I_BT, ui::I_WIFI, ui::I_GEAR, ui::I_HELP};
  for (int t = 0; t < kTiles; t++) {
    int x = 208 + (t % 2) * 95, y = 110 + (t / 2) * 56;
    bool s = homeFocus == 100 + t;
    if (s) gfx::fillRect(x, y, 89, 52, true);
    u.setDrawColor(s ? gfx::paper() : gfx::ink());
    u.drawRFrame(x, y, 89, 52, 5);
    ui::drawIcon(icons[t], x + 44, y + 20, 22);
    u.setFont(u8g2_font_helvB08_tf);
    int w = u.getUTF8Width(labels[t]);
    u.drawUTF8(x + (89 - w) / 2, y + 46, labels[t]);
    u.setDrawColor(gfx::ink());
  }
  u.drawHLine(0, LCD_H - 16, LCD_W);
  u.setFont(u8g2_font_helvR08_tf);
  u.drawUTF8(8, LCD_H - 4, "Arrows move   Enter open   N new   F files   B Bluetooth   W Wi-Fi   S settings   H help");
}

static void homeFrame() {
  Frame f;
  f.title = "Scripta";
  f.tickMs = 5000;
  f.onTick = []() { ui::markDirty(); };
  f.onKey = homeKey;
  f.customDraw = homeDraw;
  homeFocus = 0;
  ui::push(f);
}

// ------------------------------------------------------------------ editor

static void changeSize(int d) {
  DocFormat &f = S.fmt[doc.type()];
  int ns = constrain((int)f.size + d, 0, fonts::LEVELS - 1);
  if (ns == f.size) return;
  f.size = ns;
  settingsSave();
  doc.relayout();
  toast(String(kSizePt[ns]) + " pt");
}

static void nextFont() {
  DocFormat &f = S.fmt[doc.type()];
  int n = fonts::faceCount();
  int cur = (fonts::faceIndex(f.face) + 1) % n;
  strlcpy(f.face, fonts::faceName(cur), sizeof(f.face));
  settingsSave();
  doc.relayout();
  toast(f.face);
}

static void setGoal() {
  ui::askText("Word goal for this session (0 = none)", String(S.goalWords), false, [](const String &v) {
    S.goalWords = constrain(v.toInt(), 0, 100000);
    settingsSave();
  });
}

static void editorKey(const Key &k) {
  if (k.ctrl()) {
    switch (k.code) {
      case 's': saveDoc(false); return;
      case 'o': browserFrame(curDir); return;
      case 'n': newFileFlow(); return;
      case 'z': k.shift() ? doc.redo() : doc.undo(); return;
      case 'y': doc.redo(); return;
      case 'b': doc.toggleBold(); return;
      case 'i': doc.toggleItalic(); return;
      case '1': doc.heading(1); return;
      case '2': doc.heading(2); return;
      case '3': doc.heading(3); return;
      case '0': doc.heading(0); return;
      case 'l': doc.toggleBullet(); return;
      case 'k': doc.toggleNumbered(); return;
      case '=': changeSize(1); return;
      case '-': changeSize(-1); return;
      case 'f': nextFont(); return;
      case 'd':
        S.inverted = !S.inverted;
        gfx::setInverted(S.inverted);
        settingsSave();
        return;
      case 'g': setGoal(); return;
      case 'r': runSync(); return;
      case 'p': mainMenu(); return;
      case 'q': sleepNow(); return;
      case '.': S.showStatus = !S.showStatus; settingsSave(); return;
      case 'h': helpFrame(); return;
      case ',': settingsFrame(); return;
      case K_ENTER: doc.toggleCheckbox(); return;
      case K_LEFT: doc.move(Doc::WORDL); return;
      case K_RIGHT: doc.move(Doc::WORDR); return;
      case K_HOME: doc.move(Doc::DOCSTART); return;
      case K_END: doc.move(Doc::DOCEND); return;
      case K_BACKSPACE: doc.backspace(true); return;
      default: Serial.printf("[key] unhandled ctrl code=%u\n", (unsigned)k.code); return;
    }
  }
  switch (k.code) {
    case K_ENTER: doc.enter(); break;
    case K_BACKSPACE: doc.backspace(false); break;
    case K_DELETE: doc.del(); break;
    case K_TAB: doc.tab(k.shift()); break;
    case K_ESC: mainMenu(); break;
    case K_LEFT: doc.move(Doc::LEFT); break;
    case K_RIGHT: doc.move(Doc::RIGHT); break;
    case K_UP: doc.move(Doc::UP); break;
    case K_DOWN: doc.move(Doc::DOWN); break;
    case K_HOME: doc.move(Doc::HOME); break;
    case K_END: doc.move(Doc::END); break;
    case K_PGUP: doc.move(Doc::PGUP); break;
    case K_PGDN: doc.move(Doc::PGDN); break;
    case K_F1: helpFrame(); break;
    case K_F5: runSync(); break;
    default:
      if (k.printable()) doc.typeChar(k.code);
      break;
  }
}

static const uint8_t *kStatusFont = u8g2_font_helvR08_tf;
static const int kBarH = 14;

static void drawStatus() {
  U8G2 &u = gfx::u();
  int y0 = LCD_H - kBarH;
  gfx::fillRect(0, y0, LCD_W, kBarH, false);
  u.setDrawColor(gfx::ink());
  u.drawHLine(0, y0, LCD_W);
  u.setFont(kStatusFont);
  int base = LCD_H - 3;
  // right side, built right to left
  int x = LCD_W - 4;
  auto putR = [&](const String &s) {
    int w = u.getUTF8Width(s.c_str());
    x -= w;
    u.drawUTF8(x, base, s.c_str());
    x -= 8;
  };
  putR(String(batPct) + "%");
  String c = clockText();
  if (c.length()) putR(c);
  if (net::online()) putR("WiFi");
  if (kb::connected()) putR("BT");
  else if (kb::state() == kb::ST_CONNECTING || kb::state() == kb::ST_SCANNING) putR("bt...");
  // left: name
  String name = storage::baseName(doc.path());
  if (doc.dirty()) name = "* " + name;
  int w = doc.words();
  String wc = String(w) + " words";
  if (S.goalWords) {
    int made = max(0, w - sessionStartWords);
    wc += "  " + String(made) + "/" + String(S.goalWords);
  }
  int wcw = u.getUTF8Width(wc.c_str());
  int nameMax = x - wcw - 16;
  while (name.length() > 4 && u.getUTF8Width(name.c_str()) > nameMax) name.remove(name.length() - 1);
  if (toastMsg.length() && millis() < toastUntil) {
    gfx::fillRect(0, y0, LCD_W, kBarH, true);
    u.setDrawColor(gfx::paper());
    u.drawUTF8(6, base, toastMsg.c_str());
    u.setDrawColor(gfx::ink());
    return;
  }
  u.drawUTF8(4, base, name.c_str());
  u.drawUTF8(x - wcw, base, wc.c_str());
  if (S.goalWords) {
    int made = max(0, w - sessionStartWords);
    int bw = min(LCD_W, LCD_W * made / S.goalWords);
    u.drawHLine(0, y0 - 1, bw);
    u.drawHLine(0, y0 - 2, bw);
  }
}

static void drawEditor() {
  gfx::clear();
  int h = LCD_H - (S.showStatus ? kBarH : 0);
  doc.render(0, h);
  if (S.showStatus) drawStatus();
  else if (toastMsg.length() && millis() < toastUntil) {
    U8G2 &u = gfx::u();
    gfx::fillRect(0, LCD_H - kBarH, LCD_W, kBarH, true);
    u.setDrawColor(gfx::paper());
    u.setFont(kStatusFont);
    u.drawUTF8(6, LCD_H - 3, toastMsg.c_str());
    u.setDrawColor(gfx::ink());
  }
}

// ------------------------------------------------------------------ sleep

static void sleepNow() {
  saveDoc();
  settingsSave();
  kb::stop();
  net::off();
  delay(300);
  sleeping = true;
  toastMsg = "Asleep - press KEY to wake";
  toastUntil = millis() + 3600000UL;
  gfx::clear();
  if (doc.open()) drawEditor();
  else {
    gfx::u().setFont(u8g2_font_helvB18_tf);
    gfx::u().drawUTF8(110, 150, "Scripta asleep");
  }
  gfx::flush();
  delay(100);
  uint64_t mask = (1ULL << PIN_KEY) | (1ULL << PIN_BOOT);
  rtc_gpio_pullup_en((gpio_num_t)PIN_KEY);
  rtc_gpio_pullup_en((gpio_num_t)PIN_BOOT);
  esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_deep_sleep_start();
}

// ------------------------------------------------------------------ main

static void centered(const char *text, int y) {
  U8G2 &u = gfx::u();
  u.drawUTF8((LCD_W - u.getUTF8Width(text)) / 2, y, text);
}

static void splash(const String &status) {
  gfx::clear();
  U8G2 &u = gfx::u();
  u.drawFrame(10, 10, 380, 280);
  u.drawFrame(14, 14, 372, 272);
  u.setFont(u8g2_font_ncenB24_tf);
  centered("Scripta", 140);
  u.drawHLine(130, 156, 140);
  u.drawHLine(130, 158, 140);
  u.setFont(u8g2_font_ncenR14_tf);
  centered("a writerdeck", 184);
  u.setFont(u8g2_font_helvR08_tf);
  centered(status.c_str(), 258);
  centered("v" SCRIPTA_VERSION, 274);
  gfx::flush();
}

static const uint32_t kSplashMs = 6000;

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  gfx::begin();
  bool woke = esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED;
  splash("starting...");
  settingsLoad();
  gfx::setInverted(S.inverted);
  bool sd = storage::begin();
  rtc::begin();
  fonts::begin();
  meta::load();
  kb::begin();
  lastInput = millis();
  batPct = readBattery();
  if (!woke) {
    splash(String(sd ? "SD card ready" : "no SD card found") + "   -   press any key to continue");
    // show the splash for a few seconds; any key or button skips it
    Key k;
    while (millis() - t0 < kSplashMs) {
      kb::poll();
      if (kb::pop(k)) break;
      delay(20);
    }
  }
  lastInput = millis();
  if (woke && !meta::recents().empty()) openDoc(meta::recents()[0].path);
  else {
    homeFrame();
    ui::runDeferred();
    if (!sd) toast("No SD card found", 4000);
  }
  redraw = true;
}

// Shortcuts that work from any screen, not just while writing.
static bool globalKey(const Key &k) {
  if (!k.ctrl() || ui::modalActive()) return false;
  switch (k.code) {
    case 'o': ui::popAll(); ui::defer([]() { browserFrame(curDir); }); return true;
    case 'n': ui::popAll(); ui::defer(newFileFlow); return true;
    case ',': ui::popAll(); ui::defer(settingsFrame); return true;
    case 'h': ui::popAll(); ui::defer(helpFrame); return true;
    case 'r': ui::popAll(); ui::defer(runSync); return true;
    case 'q': sleepNow(); return true;
    case 'p':
      if (doc.open()) { ui::popAll(); ui::defer(mainMenu); }
      return true;
    default: return false;
  }
}

static void dispatch(const Key &k) {
  Serial.printf("[key] code=%u mods=%u\n", (unsigned)k.code, (unsigned)k.mods);
  if (globalKey(k)) return;
  if (ui::active()) {
    ui::handleKey(k);
  } else if (doc.open()) {
    editorKey(k);
  } else {
    homeFrame();
  }
}

void loop() {
  kb::poll();
  Key k;
  bool any = false;
  while (kb::pop(k)) {
    any = true;
    lastInput = millis();
    sleeping = false;
    dispatch(k);
    ui::runDeferred();
  }
  if (any) redraw = true;

  if (kb::sleepRequested()) {
    kb::clearSleepRequest();
    sleepNow();
  }

  if (kb::btMenuRequested()) {
    // BOOT+KEY: jump straight to the Bluetooth screen and start scanning
    lastInput = millis();
    ui::popAll();
    ui::defer([]() {
      btFrame();
      kb::startScan();
    });
    ui::runDeferred();
    redraw = true;
  }

  ui::tick();
  ui::runDeferred();
  if (!ui::active() && !doc.open()) {
    homeFrame();
    ui::runDeferred();
  }
  if (web::running() || net::apActive()) web::loop();

  uint32_t now = millis();

  // autosave
  if (doc.open() && doc.dirty() && now - doc.lastEditMs() > 2500 && now - lastSaveMs > (uint32_t)S.autosaveSec * 1000UL) saveDoc();

  // periodic status refresh
  if (now - lastBatMs > 10000) {
    lastBatMs = now;
    batPct = readBattery();
    redraw = true;
  }
  if (now - lastStatusMs > 1000) {
    lastStatusMs = now;
    ui::Status st;
    st.battery = batPct;
    st.bt = kb::connected();
    st.wifi = net::online();
    st.clock = clockText();
    ui::setStatus(st);
  }
  struct tm t;
  if (rtc::localTime(t) && t.tm_min != lastMinute) {
    lastMinute = t.tm_min;
    redraw = true;
  }
  if (toastMsg.length() && now >= toastUntil && toastUntil) {
    toastMsg = "";
    toastUntil = 0;
    redraw = true;
  }

  // auto sync while idle
  if (S.autoSync && davsync::configured() && !ui::active() && doc.open() && !doc.dirty() && now - lastInput > 30000UL &&
      now - lastSyncMs > (uint32_t)S.autoSyncMin * 60000UL && !web::running()) {
    lastSyncMs = now;
    runSync();
  }

  // idle sleep
  if (S.sleepMin && now - lastInput > (uint32_t)S.sleepMin * 60000UL && !web::running() && !net::apActive()) sleepNow();

  if (ui::needsRedraw()) redraw = true;
  if (redraw) {
    redraw = false;
    if (ui::active()) {
      if (!doc.open() || ui::depth() > 0) {
        ui::draw();
      } else {
        drawEditor();
        ui::draw();
      }
    } else if (doc.open()) {
      drawEditor();
    }
    gfx::flush();
  }
  delay(4);
}

}  // namespace app
