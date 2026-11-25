#include "ui.h"
#include <deque>
#include "config.h"
#include "gfx.h"

namespace ui {

static std::deque<Frame> stack;
static std::vector<std::function<void()>> deferred;
static bool dirty = true;
static Status status;

static const uint8_t *kTitleFont = u8g2_font_helvB12_tf;
static const uint8_t *kItemFont = u8g2_font_helvR12_tf;
static const uint8_t *kSmallFont = u8g2_font_helvR08_tf;
static const uint8_t *kSmallBold = u8g2_font_helvB08_tf;
static const int kRowH = 22, kListTop = 30, kHintH = 16, kHeadH = 26;

// ---- modal state ------------------------------------------------------------------
enum ModalKind { M_NONE, M_TEXT, M_YESNO, M_MSG };
static ModalKind modal = M_NONE;
static String mTitle, mText;
static int mCur = 0;
static bool mMask = false, mShow = false;
static std::function<void(const String &)> mOk;
static std::function<void()> mYes;

void defer(std::function<void()> fn) { deferred.push_back(std::move(fn)); }

void runDeferred() {
  while (!deferred.empty()) {
    auto fns = std::move(deferred);
    deferred.clear();
    for (auto &f : fns) f();
  }
}

static void settle(Frame &f) {
  if (f.items.empty()) { f.sel = 0; return; }
  f.sel = constrain(f.sel, 0, (int)f.items.size() - 1);
  int n = f.items.size(), guard = 0;
  while (f.items[f.sel].header && guard++ < n) f.sel = (f.sel + 1) % n;
}

static void rebuild(Frame &f) {
  f.items.clear();
  if (f.build) f.build(f.items);
  settle(f);
  dirty = true;
}

void push(Frame f) {
  defer([f]() mutable {
    f.sel = 0;
    f.top = 0;
    f.lastTick = millis();
    stack.push_back(std::move(f));
    rebuild(stack.back());
  });
}

void pop() {
  defer([]() {
    if (stack.empty()) return;
    auto cl = stack.back().onClose;
    stack.pop_back();
    if (!stack.empty()) rebuild(stack.back());
    dirty = true;
    if (cl) cl();
  });
}

void popAll() {
  defer([]() {
    while (!stack.empty()) {
      auto cl = stack.back().onClose;
      stack.pop_back();
      if (cl) cl();
    }
    dirty = true;
  });
}

void refresh() {
  defer([]() {
    if (!stack.empty()) rebuild(stack.back());
  });
}

void setStatus(const Status &s) {
  if (s.battery != status.battery || s.bt != status.bt || s.wifi != status.wifi || s.clock != status.clock) dirty = true;
  status = s;
}

bool active() { return !stack.empty() || modal != M_NONE; }
size_t depth() { return stack.size(); }
String selectedLabel() {
  if (stack.empty()) return "";
  Frame &f = stack.back();
  return (f.sel >= 0 && f.sel < (int)f.items.size()) ? f.items[f.sel].label : String("");
}
bool modalActive() { return modal != M_NONE; }
void markDirty() { dirty = true; }
bool needsRedraw() {
  bool d = dirty;
  dirty = false;
  return d;
}

// ---- modals --------------------------------------------------------------------------
void askText(const String &title, const String &initial, bool mask, std::function<void(const String &)> ok) {
  modal = M_TEXT;
  mTitle = title;
  mText = initial;
  mCur = initial.length();
  mMask = mask;
  mShow = false;
  mOk = ok;
  dirty = true;
}
void askYesNo(const String &msg, std::function<void()> yes) {
  modal = M_YESNO;
  mTitle = msg;
  mYes = yes;
  dirty = true;
}
void message(const String &title, const String &body) {
  modal = M_MSG;
  mTitle = title;
  mText = body;
  dirty = true;
}

static void closeModal() {
  modal = M_NONE;
  mOk = nullptr;
  mYes = nullptr;
  dirty = true;
}

static void modalKey(const Key &k) {
  if (modal == M_MSG) {
    if (k.code == K_ENTER || k.code == K_ESC || k.code == ' ') closeModal();
    return;
  }
  if (modal == M_YESNO) {
    if (k.code == 'y' || k.code == 'Y' || k.code == K_ENTER) {
      auto f = mYes;
      closeModal();
      if (f) defer(f);
    } else if (k.code == 'n' || k.code == 'N' || k.code == K_ESC) {
      closeModal();
    }
    return;
  }
  if (k.code == K_ESC) { closeModal(); return; }
  if (k.code == K_ENTER) {
    auto f = mOk;
    String v = mText;
    closeModal();
    if (f) defer([f, v]() { f(v); });
    return;
  }
  if (k.code == K_TAB) { mShow = !mShow; dirty = true; return; }
  if (k.code == K_LEFT) { if (mCur > 0) mCur--; }
  else if (k.code == K_RIGHT) { if (mCur < (int)mText.length()) mCur++; }
  else if (k.code == K_HOME) mCur = 0;
  else if (k.code == K_END) mCur = mText.length();
  else if (k.code == K_BACKSPACE) {
    if (mCur > 0) { mText.remove(mCur - 1, 1); mCur--; }
  } else if (k.code == K_DELETE) {
    if (mCur < (int)mText.length()) mText.remove(mCur, 1);
  } else if (k.ctrl() && k.code == 'u') {
    mText = "";
    mCur = 0;
  } else if (k.printable() && k.code < 128) {
    mText = mText.substring(0, mCur) + (char)k.code + mText.substring(mCur);
    mCur++;
  }
  dirty = true;
}

// ---- frame input -------------------------------------------------------------------------
static void step(Frame &f, int dir) {
  int n = f.items.size();
  if (!n) return;
  for (int i = 0; i < n; i++) {
    f.sel = (f.sel + dir + n) % n;
    if (!f.items[f.sel].header) break;
  }
}

void handleKey(const Key &k) {
  dirty = true;
  if (modal != M_NONE) { modalKey(k); return; }
  if (stack.empty()) return;
  Frame &f = stack.back();
  if (f.onKey && f.onKey(k)) return;
  if (k.ctrl() || k.alt()) return;  // shortcuts are the app's business
  int n = f.items.size();
  switch (k.code) {
    case K_UP: step(f, -1); break;
    case K_DOWN: step(f, 1); break;
    case K_PGUP: f.sel = max(0, f.sel - 8); settle(f); break;
    case K_PGDN: if (n) f.sel = min(n - 1, f.sel + 8); settle(f); break;
    case K_HOME: f.sel = 0; settle(f); break;
    case K_END: if (n) { f.sel = n - 1; if (f.items[f.sel].header) step(f, -1); } break;
    case K_ENTER:
    case ' ':
      if (n && f.items[f.sel].enter) { auto fn = f.items[f.sel].enter; fn(); }
      else if (n && f.items[f.sel].adjust) { auto fn = f.items[f.sel].adjust; fn(1); }
      break;
    case K_RIGHT:
      if (n && f.items[f.sel].adjust) { auto fn = f.items[f.sel].adjust; fn(1); }
      break;
    case K_LEFT:
      if (n && f.items[f.sel].adjust) { auto fn = f.items[f.sel].adjust; fn(-1); }
      else pop();
      break;
    case K_ESC:
    case K_BACKSPACE:
      pop();
      break;
    default: break;
  }
}

void tick() {
  if (stack.empty() || modal != M_NONE) return;
  Frame &f = stack.back();
  if (f.onTick && f.tickMs && millis() - f.lastTick >= f.tickMs) {
    f.lastTick = millis();
    f.onTick();
  }
}

// ---- drawing helpers ------------------------------------------------------------------------------
static String fit(U8G2 &u, const String &s, int maxW) {
  if (u.getUTF8Width(s.c_str()) <= maxW) return s;
  String o = s;
  while (o.length() > 1 && u.getUTF8Width((o + "...").c_str()) > maxW) o.remove(o.length() - 1);
  return o + "...";
}

static void drawBox(int x, int y, int w, int h) {
  U8G2 &u = gfx::u();
  gfx::fillRect(x, y, w, h, false);
  u.setDrawColor(gfx::ink());
  u.drawFrame(x, y, w, h);
  u.drawFrame(x + 1, y + 1, w - 2, h - 2);
}

void drawBattery(int x, int y, int pct) {
  U8G2 &u = gfx::u();
  u.drawFrame(x, y, 20, 10);
  u.drawBox(x + 20, y + 3, 2, 4);
  int w = constrain(pct, 0, 100) * 16 / 100;
  if (w > 0) u.drawBox(x + 2, y + 2, w, 6);
}

// All icons are drawn on a 20-unit grid around (cx, cy) and scaled to `size`.
void drawIcon(Icon ic, int cx, int cy, int size) {
  U8G2 &u = gfx::u();
  auto S = [size](int v) { return v * size / 20; };
  auto line = [&](int x0, int y0, int x1, int y1) { u.drawLine(cx + S(x0), cy + S(y0), cx + S(x1), cy + S(y1)); };
  auto rect = [&](int x, int y, int w, int h) { u.drawFrame(cx + S(x), cy + S(y), max(1, S(w)), max(1, S(h))); };
  switch (ic) {
    case I_FILE:
    case I_TEXT:
      rect(-6, -9, 12, 18);
      line(-3, -3, 3, -3);
      line(-3, 1, 3, 1);
      line(-3, 5, 1, 5);
      break;
    case I_NEW:
      rect(-6, -9, 12, 18);
      line(0, -4, 0, 4);
      line(-4, 0, 4, 0);
      break;
    case I_FOLDER:
      rect(-9, -5, 18, 12);
      line(-9, -5, -7, -8);
      line(-7, -8, -2, -8);
      line(-2, -8, 0, -5);
      break;
    case I_WIFI:
      for (int r = 4; r <= 12; r += 4) u.drawCircle(cx, cy + S(6), max(2, S(r)), U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
      u.drawDisc(cx, cy + S(6), max(1, S(1)));
      break;
    case I_BT:
      line(0, -9, 0, 9);
      line(0, -9, 6, -3);
      line(6, -3, -6, 5);
      line(0, 9, 6, 3);
      line(6, 3, -6, -5);
      break;
    case I_GEAR:
      u.drawCircle(cx, cy, max(2, S(5)));
      u.drawDisc(cx, cy, max(1, S(2)));
      for (int a = 0; a < 8; a++) {
        static const int dx[8] = {0, 7, 10, 7, 0, -7, -10, -7};
        static const int dy[8] = {-10, -7, 0, 7, 10, 7, 0, -7};
        int x0 = dx[a] * 6 / 10, y0 = dy[a] * 6 / 10;
        line(x0, y0, dx[a], dy[a]);
      }
      break;
    case I_HELP:
      u.drawCircle(cx, cy, max(3, S(9)));
      u.setFont(u8g2_font_helvB10_tf);
      u.drawStr(cx - 3, cy + 5, "?");
      break;
    case I_SYNC:
      line(-8, -3, 6, -3);
      line(6, -3, 3, -6);
      line(6, -3, 3, 0);
      line(8, 3, -6, 3);
      line(-6, 3, -3, 0);
      line(-6, 3, -3, 6);
      break;
    case I_SLEEP:
      u.drawDisc(cx, cy, max(2, S(8)));
      u.setDrawColor(gfx::paper());
      u.drawDisc(cx + S(4), cy - S(3), max(2, S(7)));
      u.setDrawColor(gfx::ink());
      break;
    case I_BACK:
      line(8, 0, -6, 0);
      line(-6, 0, -1, -5);
      line(-6, 0, -1, 5);
      break;
    case I_CHECK:
      line(-7, 0, -2, 6);
      line(-2, 6, 8, -6);
      break;
    case I_FONT:
      u.setFont(size <= 16 ? u8g2_font_ncenB08_tf : u8g2_font_ncenB14_tf);
      u.drawStr(cx - u.getStrWidth("Aa") / 2, cy + u.getAscent() / 2, "Aa");
      break;
    case I_CLOSE:
      line(-6, -6, 6, 6);
      line(-6, 6, 6, -6);
      break;
    case I_RESUME:
      u.drawTriangle(cx - S(5), cy - S(8), cx - S(5), cy + S(8), cx + S(7), cy);
      break;
    case I_BATT:
      u.drawFrame(cx - S(9), cy - S(4), max(2, S(16)), max(2, S(9)));
      u.drawBox(cx + S(7), cy - S(2), max(1, S(2)), max(1, S(5)));
      break;
    case I_GOAL:
      u.drawCircle(cx, cy, max(3, S(8)));
      u.drawCircle(cx, cy, max(2, S(4)));
      u.drawDisc(cx, cy, max(1, S(1)));
      break;
    case I_CLOCK:
      u.drawCircle(cx, cy, max(3, S(8)));
      line(0, 0, 0, -5);
      line(0, 0, 4, 2);
      break;
    case I_INFO:
      u.drawCircle(cx, cy, max(3, S(9)));
      u.drawVLine(cx, cy - S(1), max(2, S(5)));
      u.drawPixel(cx, cy - S(4));
      break;
    default: break;
  }
}

void drawHeader(const String &title) {
  U8G2 &u = gfx::u();
  gfx::fillRect(0, 0, LCD_W, kHeadH - 2, true);
  u.setDrawColor(gfx::paper());
  // status chips on the right, built right to left
  int x = LCD_W - 6;
  u.setFont(kSmallBold);
  if (status.clock.length()) {
    int w = u.getUTF8Width(status.clock.c_str());
    x -= w;
    u.drawUTF8(x, 16, status.clock.c_str());
    x -= 10;
  }
  String pct = String(status.battery) + "%";
  int pw = u.getUTF8Width(pct.c_str());
  x -= pw;
  u.drawUTF8(x, 16, pct.c_str());
  x -= 25;
  // battery glyph in paper colour on the ink bar
  u.drawFrame(x, 6, 20, 10);
  u.drawBox(x + 20, 9, 2, 4);
  int bw = constrain(status.battery, 0, 100) * 16 / 100;
  if (bw > 0) u.drawBox(x + 2, 8, bw, 6);
  x -= 8;
  if (status.wifi) {
    drawIcon(I_WIFI, x - 8, 9, 16);
    x -= 24;
  }
  if (status.bt) {
    drawIcon(I_BT, x - 4, 11, 16);
    x -= 20;
  }
  u.setFont(kTitleFont);
  u.drawUTF8(8, 18, fit(u, title, x - 16).c_str());
  u.setDrawColor(gfx::ink());
}

static void drawModal() {
  U8G2 &u = gfx::u();
  if (modal == M_TEXT) {
    int w = 360, h = 92, x = (LCD_W - w) / 2, y = (LCD_H - h) / 2;
    drawBox(x, y, w, h);
    u.setFont(kTitleFont);
    u.drawUTF8(x + 12, y + 22, fit(u, mTitle, w - 24).c_str());
    u.setFont(kItemFont);
    String shown = mText;
    if (mMask && !mShow) { shown = ""; for (size_t i = 0; i < mText.length(); i++) shown += '*'; }
    int tw = w - 24;
    String left = shown.substring(0, mCur);
    int cw = u.getUTF8Width(left.c_str());
    int off = cw > tw - 6 ? cw - (tw - 6) : 0;
    u.setClipWindow(x + 12, y + 30, x + w - 12, y + 56);
    u.drawUTF8(x + 12 - off, y + 50, shown.c_str());
    u.drawVLine(x + 12 + cw - off, y + 36, 18);
    u.setMaxClipWindow();
    u.drawHLine(x + 12, y + 56, tw);
    u.setFont(kSmallFont);
    u.drawStr(x + 12, y + h - 10, mMask ? "Enter OK   Esc cancel   Tab show/hide" : "Enter OK   Esc cancel   Ctrl+U clear");
  } else if (modal == M_YESNO) {
    int w = 340, h = 80, x = (LCD_W - w) / 2, y = (LCD_H - h) / 2;
    drawBox(x, y, w, h);
    u.setFont(kItemFont);
    u.drawUTF8(x + 12, y + 28, fit(u, mTitle, w - 24).c_str());
    u.setFont(kSmallFont);
    u.drawStr(x + 12, y + h - 14, "Y / Enter = yes     N / Esc = no");
  } else if (modal == M_MSG) {
    int w = 360, h = 120, x = (LCD_W - w) / 2, y = (LCD_H - h) / 2;
    drawBox(x, y, w, h);
    u.setFont(kTitleFont);
    u.drawUTF8(x + 12, y + 24, fit(u, mTitle, w - 24).c_str());
    u.setFont(kItemFont);
    int ly = y + 46;
    String rest = mText;
    while (rest.length() && ly < y + h - 20) {
      int nl = rest.indexOf('\n');
      String line = nl < 0 ? rest : rest.substring(0, nl);
      rest = nl < 0 ? String("") : rest.substring(nl + 1);
      u.drawUTF8(x + 12, ly, fit(u, line, w - 24).c_str());
      ly += 17;
    }
    u.setFont(kSmallFont);
    u.drawStr(x + 12, y + h - 8, "Press Enter");
  }
}

static void drawList(Frame &f) {
  U8G2 &u = gfx::u();
  int rows = (LCD_H - kListTop - kHintH) / kRowH;
  if (f.sel < f.top) f.top = f.sel;
  if (f.sel >= f.top + rows) f.top = f.sel - rows + 1;
  // keep a section header visible above the first selected row when possible
  if (f.top > 0 && f.top == f.sel && f.items[f.top - 1].header) f.top--;
  for (int r = 0; r < rows; r++) {
    int i = f.top + r;
    if (i >= (int)f.items.size()) break;
    Item &it = f.items[i];
    int y = kListTop + r * kRowH;
    if (it.header) {
      u.setFont(kSmallBold);
      String up = it.label;
      up.toUpperCase();
      int w = u.getUTF8Width(up.c_str());
      u.drawUTF8(10, y + 15, up.c_str());
      u.drawHLine(16 + w, y + 11, LCD_W - 26 - w);
      u.drawHLine(16 + w, y + 12, LCD_W - 26 - w);
      continue;
    }
    bool sel = i == f.sel;
    if (sel) {
      gfx::fillRect(4, y, LCD_W - 12, kRowH - 2, true);
    }
    u.setDrawColor(sel ? gfx::paper() : gfx::ink());
    int x0 = 12;
    if (it.icon != I_NONE) {
      drawIcon(it.icon, 20, y + kRowH / 2 - 1, 14);
      x0 = 34;
    }
    u.setFont(kItemFont);
    bool chip = it.adjust && it.detail.length();
    String det = it.detail;
    int dw = det.length() ? u.getUTF8Width(det.c_str()) : 0;
    int maxLabel = LCD_W - x0 - 14 - (dw ? dw + (chip ? 24 : 12) : 0);
    String lab = fit(u, it.label, maxLabel);
    u.drawUTF8(x0, y + 15, lab.c_str());
    if (dw) {
      int rx = LCD_W - 14 - dw;
      u.drawUTF8(rx, y + 15, det.c_str());
    }
    if (it.dim && !sel) {
      for (int x = x0; x < x0 + u.getUTF8Width(lab.c_str()); x += 3) u.drawPixel(x, y + 18);
    }
    u.setDrawColor(gfx::ink());
  }
  if (f.items.size() > (size_t)rows) {
    int bh = (LCD_H - kListTop - kHintH);
    int th = max(8, bh * rows / (int)f.items.size());
    int ty = kListTop + (bh - th) * f.top / max(1, (int)f.items.size() - rows);
    u.drawBox(LCD_W - 3, ty, 2, th);
  }
}

void draw() {
  if (!stack.empty()) {
    Frame &f = stack.back();
    U8G2 &u = gfx::u();
    gfx::clear();
    if (f.customDraw) {
      f.customDraw();
    } else {
      drawHeader(f.title);
      drawList(f);
      u.drawHLine(0, LCD_H - kHintH, LCD_W);
      u.setFont(kSmallFont);
      String hint = f.hint.length() ? f.hint : String("Up/Down move    Enter select    Left/Right change    Esc back");
      u.drawUTF8(8, LCD_H - 4, fit(u, hint, LCD_W - 16).c_str());
    }
  }
  if (modal != M_NONE) drawModal();
}

void busy(const String &title, const std::vector<String> &lines) {
  U8G2 &u = gfx::u();
  int w = 360, h = 40 + 17 * min((int)lines.size(), 6), x = (LCD_W - w) / 2, y = (LCD_H - h) / 2;
  drawBox(x, y, w, h);
  u.setFont(kTitleFont);
  u.drawUTF8(x + 12, y + 24, fit(u, title, w - 24).c_str());
  u.setFont(kItemFont);
  int start = max(0, (int)lines.size() - 6);
  for (int i = start; i < (int)lines.size(); i++)
    u.drawUTF8(x + 12, y + 42 + (i - start) * 17, fit(u, lines[i], w - 24).c_str());
  gfx::flush();
}

// ---- item helpers ---------------------------------------------------------------------------------------
Item item(const String &label, const String &detail, std::function<void()> enter, Icon icon) {
  Item i;
  i.label = label;
  i.detail = detail;
  i.enter = enter;
  i.icon = icon;
  return i;
}

Item section(const String &label) {
  Item i;
  i.label = label;
  i.header = true;
  return i;
}

Item toggleItem(const String &label, bool value, std::function<void(bool)> set, Icon icon) {
  Item i;
  i.label = label;
  i.detail = value ? "[x] On" : "[ ] Off";
  i.icon = icon;
  i.adjust = [set, value](int) { set(!value); refresh(); };
  i.enter = [set, value]() { set(!value); refresh(); };
  return i;
}

Item choiceItem(const String &label, const String &value, std::function<void(int)> change, Icon icon) {
  Item i;
  i.label = label;
  i.detail = String("< ") + value + " >";
  i.icon = icon;
  i.adjust = [change](int d) { change(d); refresh(); };
  i.enter = [change]() { change(1); refresh(); };
  return i;
}

}  // namespace ui
