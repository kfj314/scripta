#pragma once
#include <Arduino.h>
#include <functional>
#include <vector>
#include "keys.h"

// Small menu/dialog toolkit used by every non-editor screen. Structural
// changes (push/pop/refresh) are deferred until the current key handler has
// returned, so a menu item can safely replace the menu that is running it.
namespace ui {

enum Icon : uint8_t {
  I_NONE, I_FILE, I_FOLDER, I_NEW, I_WIFI, I_BT, I_GEAR, I_HELP, I_SYNC, I_SLEEP,
  I_TEXT, I_BACK, I_CHECK, I_FONT, I_CLOSE, I_RESUME, I_BATT, I_GOAL, I_CLOCK, I_INFO
};

struct Item {
  String label;
  String detail;
  std::function<void()> enter;
  std::function<void(int)> adjust;  // Left/Right (-1/+1)
  bool dim = false;
  bool header = false;  // section label: drawn as a divider, never selected
  Icon icon = I_NONE;
};

struct Frame {
  String title;
  String hint;
  std::function<void(std::vector<Item> &)> build;
  std::function<bool(const Key &)> onKey;  // return true if handled
  std::function<void()> onTick;            // called ~every tickMs while on top
  std::function<void()> onClose;
  std::function<void()> customDraw;        // replaces the list (Home screen)
  uint32_t tickMs = 0;
  // runtime
  std::vector<Item> items;
  int sel = 0;
  int top = 0;
  uint32_t lastTick = 0;
};

struct Status {
  int battery = 100;
  bool bt = false;
  bool wifi = false;
  String clock;
};

void push(Frame f);
void pop();
void popAll();
void refresh();
bool active();  // any frame or modal showing
size_t depth();
String selectedLabel();
void defer(std::function<void()> fn);
void runDeferred();
void setStatus(const Status &s);

// modals
void askText(const String &title, const String &initial, bool mask, std::function<void(const String &)> ok);
void askYesNo(const String &msg, std::function<void()> yes);
void message(const String &title, const String &body);
bool modalActive();

// events
void handleKey(const Key &k);
void tick();
bool needsRedraw();
void markDirty();
void draw();  // draws top frame + modal into the frame buffer (no flush)

// drawing helpers (also used by the Home screen)
void drawIcon(Icon ic, int cx, int cy, int size);
void drawHeader(const String &title);
void drawBattery(int x, int y, int pct);

// helpers
Item item(const String &label, const String &detail, std::function<void()> enter, Icon icon = I_NONE);
Item section(const String &label);
Item toggleItem(const String &label, bool value, std::function<void(bool)> set, Icon icon = I_NONE);
Item choiceItem(const String &label, const String &value, std::function<void(int)> change, Icon icon = I_NONE);

void busy(const String &title, const std::vector<String> &lines);  // draws and flushes immediately

}  // namespace ui
