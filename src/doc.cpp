#include "doc.h"
#include "gfx.h"
#include "storage.h"

static const uint8_t ST_BOLD = 1, ST_ITAL = 2, ST_HID = 0x80;

static inline uint32_t dec(const char *s, int n, int &i) {
  uint8_t c = s[i++];
  if (c < 0x80) return c;
  int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
  uint32_t cp = extra == 3 ? (c & 7) : extra == 2 ? (c & 15) : extra == 1 ? (c & 31) : 0xFFFD;
  while (extra-- > 0 && i < n && (s[i] & 0xC0) == 0x80) cp = (cp << 6) | (s[i++] & 0x3F);
  return cp;
}

static std::string enc(uint32_t cp) {
  std::string o;
  if (cp < 0x80) o += (char)cp;
  else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
  else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
  return o;
}

static bool isList(Block b) { return b == B_BULLET || b == B_NUM || b == B_TODO; }
static bool hasPrefixMarker(Block b) { return b == B_H1 || b == B_H2 || b == B_H3 || isList(b) || b == B_HR; }

// ---------------------------------------------------------------- loading

bool Doc::load(const String &path, DocType type) {
  storage::recoverTmp(path);
  std::string raw;
  if (!storage::readFile(path, raw)) return false;
  text_.clear();
  text_.reserve(raw.size() + 8192);
  for (char c : raw)
    if (c != '\r') text_ += c;
  path_ = path;
  type_ = type;
  String lower = path;
  lower.toLowerCase();
  markdown_ = lower.endsWith(".md") || lower.endsWith(".markdown");
  dirty_ = false;
  cursor_ = 0;
  scroll_ = 0;
  wantX_ = -1;
  undo_.clear();
  redo_.clear();
  undoBytes_ = 0;
  lastActive_ = -1;
  splitAll();
  openWords_ = words();
  return true;
}

bool Doc::reload() {
  if (!open()) return false;
  std::string raw;
  if (!storage::readFile(path_, raw)) return false;
  text_.clear();
  for (char c : raw)
    if (c != '\r') text_ += c;
  if (cursor_ > text_.size()) cursor_ = text_.size();
  undo_.clear();
  redo_.clear();
  undoBytes_ = 0;
  dirty_ = false;
  lastActive_ = -1;
  splitAll();
  return true;
}

bool Doc::save() {
  if (!open()) return false;
  bool ok = storage::writeFile(path_, text_.data(), text_.size());
  if (ok) dirty_ = false;
  return ok;
}

void Doc::closeDoc() {
  path_ = "";
  text_.clear();
  paras_.clear();
  undo_.clear();
  redo_.clear();
  dirty_ = false;
}

void Doc::setType(DocType t) {
  type_ = t;
  relayout();
}

void Doc::splitAll() {
  paras_.clear();
  uint32_t s = 0;
  for (uint32_t k = 0; k <= text_.size(); k++) {
    if (k == text_.size() || text_[k] == '\n') {
      Para q;
      q.start = s;
      q.len = k - s;
      classify(q);
      paras_.push_back(std::move(q));
      s = k + 1;
    }
  }
  yDirty_ = true;
  wordsDirty_ = true;
}

int Doc::paraAt(uint32_t off) const {
  int lo = 0, hi = (int)paras_.size() - 1;
  while (lo < hi) {
    int mid = (lo + hi + 1) / 2;
    if (paras_[mid].start <= off) lo = mid;
    else hi = mid - 1;
  }
  return lo;
}

void Doc::classify(Para &pa) const {
  pa.kind = B_BODY;
  pa.depth = 0;
  pa.prefix = 0;
  pa.checked = 0;
  const char *s = text_.data() + pa.start;
  int n = pa.len;
  if (n == 0) { pa.kind = B_BLANK; return; }
  if (!markdown_) return;
  int sp = 0;
  while (sp < n && s[sp] == ' ') sp++;
  if (sp == 0 && s[0] == '#') {
    int h = 0;
    while (h < n && s[h] == '#') h++;
    if (h < n && s[h] == ' ' && h <= 6) {
      pa.kind = h == 1 ? B_H1 : h == 2 ? B_H2 : B_H3;
      pa.prefix = h + 1;
      return;
    }
  }
  if (sp == 0 && n >= 3 && (s[0] == '-' || s[0] == '*' || s[0] == '_')) {
    bool same = true;
    for (int i = 1; i < n; i++) if (s[i] != s[0]) same = false;
    if (same) { pa.kind = B_HR; pa.prefix = n; return; }
  }
  if (sp + 1 < n && (s[sp] == '-' || s[sp] == '*' || s[sp] == '+') && s[sp + 1] == ' ') {
    pa.kind = B_BULLET;
    pa.depth = sp >= 2 ? 1 : 0;
    pa.prefix = sp + 2;
    if (n >= sp + 6 && s[sp + 2] == '[' && (s[sp + 3] == ' ' || s[sp + 3] == 'x' || s[sp + 3] == 'X') && s[sp + 4] == ']' && s[sp + 5] == ' ') {
      pa.kind = B_TODO;
      pa.prefix = sp + 6;
      pa.checked = s[sp + 3] != ' ';
    }
    return;
  }
  int d = sp;
  while (d < n && s[d] >= '0' && s[d] <= '9') d++;
  if (d > sp && d - sp < 6 && d + 1 < n && (s[d] == '.' || s[d] == ')') && s[d + 1] == ' ') {
    pa.kind = B_NUM;
    pa.depth = sp >= 2 ? 1 : 0;
    pa.prefix = d + 2;
  }
}

// ---------------------------------------------------------------- editing core

void Doc::touch() {
  dirty_ = true;
  lastEdit_ = millis();
  wordsDirty_ = true;
}

void Doc::applyRaw(uint32_t pos, uint32_t del, const std::string &ins) {
  int a = paraAt(pos);
  int b = paraAt(pos + del);
  uint32_t rs = paras_[a].start;
  uint32_t oldEnd = paras_[b].start + paras_[b].len;
  text_.replace(pos, del, ins);
  int32_t delta = (int32_t)ins.size() - (int32_t)del;
  uint32_t newEnd = oldEnd + delta;
  std::vector<Para> np;
  uint32_t s = rs;
  for (uint32_t k = rs; k <= newEnd; k++) {
    if (k == newEnd || text_[k] == '\n') {
      Para q;
      q.start = s;
      q.len = k - s;
      classify(q);
      np.push_back(std::move(q));
      s = k + 1;
    }
  }
  int oldCount = b - a + 1;
  paras_.erase(paras_.begin() + a, paras_.begin() + b + 1);
  paras_.insert(paras_.begin() + a, np.begin(), np.end());
  int after = a + (int)np.size();
  for (int j = after; j < (int)paras_.size(); j++) paras_[j].start += delta;
  // following paragraphs can depend on this one (first-line indent)
  for (int j = after; j < (int)paras_.size(); j++) {
    paras_[j].h = -1;
    if (paras_[j].kind != B_BLANK) break;
  }
  if (lastActive_ > b) lastActive_ += (int)np.size() - oldCount;
  else if (lastActive_ >= a) lastActive_ = -1;
  yDirty_ = true;
}

void Doc::beginGroup() {
  inGroup_ = true;
  curGrp_ = ++grpCounter_;
}
void Doc::endGroup() { inGroup_ = false; }

void Doc::replace(uint32_t pos, uint32_t del, const std::string &ins, uint32_t curAfter, bool coalesce) {
  if (pos > text_.size()) pos = text_.size();
  if (del > text_.size() - pos) del = text_.size() - pos;
  if (del == 0 && ins.empty()) return;
  uint32_t now = millis();
  Op op;
  op.pos = pos;
  op.del = text_.substr(pos, del);
  op.ins = ins;
  op.curBefore = cursor_;
  op.curAfter = curAfter;
  op.t = now;
  op.typing = false;
  redo_.clear();
  bool merged = false;
  if (coalesce && !inGroup_ && !undo_.empty()) {
    Op &t = undo_.back();
    if (t.typing && now - t.t < 2000 && t.curAfter == cursor_) {
      if (t.del.empty() && op.del.empty() && t.pos + t.ins.size() == pos && t.ins.back() != ' ' && t.ins.back() != '\n') {
        t.ins += ins;
        t.curAfter = curAfter;
        t.t = now;
        undoBytes_ += ins.size();
        merged = true;
      } else if (t.ins.empty() && op.ins.empty() && op.pos + op.del.size() == t.pos) {
        t.pos = op.pos;
        t.del = op.del + t.del;
        t.curAfter = curAfter;
        t.t = now;
        undoBytes_ += op.del.size();
        merged = true;
      }
    }
  }
  if (!merged) {
    op.grp = inGroup_ ? curGrp_ : ++grpCounter_;
    op.typing = coalesce && !inGroup_;
    undoBytes_ += op.del.size() + op.ins.size() + 48;
    undo_.push_back(op);
    while (undoBytes_ > 120000 && undo_.size() > 1) {
      uint32_t g = undo_.front().grp;
      while (!undo_.empty() && undo_.front().grp == g) {
        undoBytes_ -= min(undoBytes_, undo_.front().del.size() + undo_.front().ins.size() + 48);
        undo_.erase(undo_.begin());
      }
    }
  }
  applyRaw(pos, del, ins);
  cursor_ = curAfter > text_.size() ? text_.size() : curAfter;
  wantX_ = -1;
  touch();
}

void Doc::undo() {
  if (undo_.empty()) return;
  uint32_t g = undo_.back().grp;
  uint32_t cur = cursor_;
  while (!undo_.empty() && undo_.back().grp == g) {
    Op op = std::move(undo_.back());
    undo_.pop_back();
    applyRaw(op.pos, op.ins.size(), op.del);
    cur = op.curBefore;
    redo_.push_back(std::move(op));
  }
  cursor_ = cur > text_.size() ? text_.size() : cur;
  wantX_ = -1;
  touch();
}

void Doc::redo() {
  if (redo_.empty()) return;
  uint32_t g = redo_.back().grp;
  uint32_t cur = cursor_;
  while (!redo_.empty() && redo_.back().grp == g) {
    Op op = std::move(redo_.back());
    redo_.pop_back();
    applyRaw(op.pos, op.del.size(), op.ins);
    cur = op.curAfter;
    undo_.push_back(std::move(op));
  }
  cursor_ = cur > text_.size() ? text_.size() : cur;
  wantX_ = -1;
  touch();
}

// ---------------------------------------------------------------- editing commands

void Doc::typeChar(uint32_t cp) {
  std::string s = enc(cp);
  replace(cursor_, 0, s, cursor_ + s.size(), true);
}

void Doc::typeText(const std::string &s) { replace(cursor_, 0, s, cursor_ + s.size()); }

static std::string spaces(int n) { return std::string(n, ' '); }

void Doc::replacePrefix(int i, const std::string &np) {
  Para &pa = paras_[i];
  uint32_t start = pa.start, oldP = pa.prefix;
  uint32_t rel = cursor_ >= start ? cursor_ - start : 0;
  uint32_t cur = rel >= oldP ? start + np.size() + (rel - oldP) : start + np.size();
  replace(start, oldP, np, cur);
}

void Doc::renumber(int i) {
  if (i < 0 || i >= (int)paras_.size()) return;
  int s = i;
  if (!isList(paras_[s].kind) && s + 1 < (int)paras_.size() && isList(paras_[s + 1].kind)) s = s + 1;
  while (s > 0 && isList(paras_[s - 1].kind)) s--;
  int c[2] = {0, 0};
  for (int j = s; j < (int)paras_.size() && isList(paras_[j].kind); j++) {
    Para &pa = paras_[j];
    int d = pa.depth;
    if (pa.kind != B_NUM) {
      c[d] = 0;
      if (d == 0) c[1] = 0;
      continue;
    }
    c[d]++;
    if (d == 0) c[1] = 0;
    const char *t = p(pa);
    int sp = d ? (int)(pa.prefix - 0) : 0;
    sp = 0;
    while (sp < (int)pa.len && t[sp] == ' ') sp++;
    int dg = sp;
    while (dg < (int)pa.len && t[dg] >= '0' && t[dg] <= '9') dg++;
    int cur = atoi(std::string(t + sp, dg - sp).c_str());
    if (cur != c[d]) {
      uint32_t save = cursor_;
      std::string ns = std::to_string(c[d]);
      uint32_t from = pa.start + sp, len = dg - sp;
      uint32_t newCur = save;
      if (save > from + len) newCur = save + ns.size() - len;
      else if (save > from) newCur = from + ns.size();
      replace(from, len, ns, newCur);
    }
  }
}

// Renumbering after a delete should undo together with the delete itself.
void Doc::renumberJoin(int i) {
  if (!markdown_ || undo_.empty()) return;
  bool wasIn = inGroup_;
  uint32_t wasGrp = curGrp_;
  inGroup_ = true;
  curGrp_ = undo_.back().grp;
  renumber(i);
  inGroup_ = wasIn;
  curGrp_ = wasGrp;
}

void Doc::enter() {
  int i = paraAt(cursor_);
  Para pa = paras_[i];
  beginGroup();
  if (isList(pa.kind) && markdown_) {
    if (cursor_ < pa.start + pa.prefix) cursor_ = pa.start + pa.prefix;
    if (pa.len == pa.prefix) {  // empty item: step out of the list / one level up
      if (pa.depth > 0) {
        replace(pa.start, 2, "", cursor_ - 2);
        renumber(i);
      } else {
        replacePrefix(i, "");
      }
      endGroup();
      return;
    }
    std::string marker = spaces(pa.depth * 2);
    if (pa.kind == B_NUM) {
      const char *t = p(pa);
      int sp = pa.depth ? 2 : 0;
      int n = atoi(std::string(t + sp, pa.prefix - sp).c_str());
      marker += std::to_string(n + 1) + ". ";
    } else if (pa.kind == B_TODO) {
      marker += "- [ ] ";
    } else {
      marker += "- ";
    }
    std::string ins = "\n" + marker;
    replace(cursor_, 0, ins, cursor_ + ins.size());
    renumber(paraAt(cursor_));
  } else if (fmt().blankLines) {
    replace(cursor_, 0, "\n\n", cursor_ + 2);
  } else {
    replace(cursor_, 0, "\n", cursor_ + 1);
  }
  endGroup();
}

bool Doc::isCollapsedBlank(int i) const {
  return fmt().tight && i >= 0 && i < (int)paras_.size() && paras_[i].kind == B_BLANK && paras_[i].h == 0;
}

void Doc::backspace(bool word) {
  if (cursor_ == 0) return;
  int i = paraAt(cursor_);
  const Para &pa = paras_[i];
  if (markdown_ && pa.prefix > 0 && hasPrefixMarker(pa.kind) && pa.kind != B_HR && cursor_ == pa.start + pa.prefix) {
    beginGroup();
    uint32_t keepDepth = pa.depth;
    bool wasNum = pa.kind == B_NUM;
    if (isList(pa.kind) && keepDepth > 0) replacePrefix(i, "");
    else replacePrefix(i, "");
    endGroup();
    (void)wasNum;
    return;
  }
  uint32_t from;
  if (word) {
    from = cursor_;
    while (from > 0 && text_[from - 1] == ' ') from--;
    while (from > 0 && text_[from - 1] != ' ' && text_[from - 1] != '\n') from--;
    if (from == cursor_ && from > 0) from--;
  } else if (cursor_ == pa.start && i > 0) {
    int back = 1;
    if (fmt().tight && paras_[i - 1].kind == B_BLANK && i > 1) back = 2;
    from = cursor_ - back;
  } else {
    from = cursor_ - 1;
    while (from > 0 && ((uint8_t)text_[from] & 0xC0) == 0x80) from--;
  }
  uint32_t n = cursor_ - from;
  replace(from, n, "", from, true);
  renumberJoin(paraAt(cursor_));
}

void Doc::del() {
  if (cursor_ >= text_.size()) return;
  int i = paraAt(cursor_);
  const Para &pa = paras_[i];
  uint32_t n = 1;
  if (cursor_ == pa.start + pa.len) {
    if (fmt().tight && i + 1 < (int)paras_.size() && paras_[i + 1].kind == B_BLANK && i + 2 < (int)paras_.size()) n = 2;
  } else {
    while (cursor_ + n < text_.size() && ((uint8_t)text_[cursor_ + n] & 0xC0) == 0x80) n++;
  }
  replace(cursor_, n, "", cursor_, true);
  renumberJoin(paraAt(cursor_));
}

void Doc::tab(bool shift) {
  int i = paraAt(cursor_);
  Para pa = paras_[i];
  if (markdown_ && isList(pa.kind)) {
    beginGroup();
    if (!shift && pa.depth == 0) {
      replace(pa.start, 0, "  ", cursor_ + 2);
    } else if (shift && pa.depth > 0) {
      replace(pa.start, 2, "", cursor_ >= pa.start + 2 ? cursor_ - 2 : pa.start);
    }
    renumber(paraAt(cursor_));
    endGroup();
    return;
  }
  if (!shift) typeText("  ");
}

void Doc::heading(int level) {
  if (!markdown_) return;
  int i = paraAt(cursor_);
  Para pa = paras_[i];
  Block want = level == 1 ? B_H1 : level == 2 ? B_H2 : B_H3;
  beginGroup();
  if (level == 0 || pa.kind == want) replacePrefix(i, "");
  else replacePrefix(i, std::string(level, '#') + " ");
  endGroup();
}

void Doc::toggleBullet() {
  if (!markdown_) return;
  int i = paraAt(cursor_);
  Para pa = paras_[i];
  beginGroup();
  if (pa.kind == B_BULLET) replacePrefix(i, "");
  else replacePrefix(i, spaces(isList(pa.kind) ? pa.depth * 2 : 0) + "- ");
  renumber(i);
  endGroup();
}

void Doc::toggleNumbered() {
  if (!markdown_) return;
  int i = paraAt(cursor_);
  Para pa = paras_[i];
  beginGroup();
  if (pa.kind == B_NUM) replacePrefix(i, "");
  else replacePrefix(i, spaces(isList(pa.kind) ? pa.depth * 2 : 0) + "1. ");
  renumber(i);
  endGroup();
}

void Doc::toggleCheckbox() {
  if (!markdown_) return;
  int i = paraAt(cursor_);
  Para pa = paras_[i];
  beginGroup();
  if (pa.kind == B_TODO) {
    uint32_t at = pa.start + pa.prefix - 3;
    replace(at, 1, pa.checked ? " " : "x", cursor_);
  } else {
    replacePrefix(i, spaces(isList(pa.kind) ? pa.depth * 2 : 0) + "- [ ] ");
  }
  endGroup();
}

void Doc::wordBounds(uint32_t &ws, uint32_t &we) const {
  ws = we = cursor_;
  while (ws > 0 && text_[ws - 1] != ' ' && text_[ws - 1] != '\n') ws--;
  while (we < text_.size() && text_[we] != ' ' && text_[we] != '\n') we++;
  // trim brackets, quotes, punctuation and any bold/italic markers off both ends
  const char *lead = "(\"'[*_";
  const char *trail = ".,;:!?)\"']*_,";
  bool changed = true;
  while (changed && ws < we) {
    changed = false;
    if (strchr(lead, text_[ws])) { ws++; changed = true; }
    if (we > ws && strchr(trail, text_[we - 1])) { we--; changed = true; }
  }
}

// Toggle a bold (**) or italic (_) mark around the word under the cursor.
void Doc::toggleWrap(const char *mark) {
  if (!markdown_) return;
  size_t m = strlen(mark);
  uint32_t ws, we;
  wordBounds(ws, we);
  beginGroup();
  if (we <= ws) {
    std::string ins = std::string(mark) + mark;
    replace(cursor_, 0, ins, cursor_ + m);
    endGroup();
    return;
  }
  auto isMark = [](char c) { return c == '*' || c == '_'; };
  uint32_t bs = ws;
  while (bs > 0 && isMark(text_[bs - 1])) bs--;
  uint32_t ae = we;
  while (ae < text_.size() && isMark(text_[ae])) ae++;
  std::string before = text_.substr(bs, ws - bs), after = text_.substr(we, ae - we);
  size_t bp = before.rfind(mark), ap = after.find(mark);
  auto shrink = [&](uint32_t at) {  // cursor position after removing m bytes at `at`
    if (cursor_ >= at + m) return cursor_ - (uint32_t)m;
    return cursor_ > at ? at : cursor_;
  };
  if (bp != std::string::npos && ap != std::string::npos) {
    uint32_t closeAt = we + ap, openAt = bs + bp;
    replace(closeAt, m, "", shrink(closeAt));
    replace(openAt, m, "", shrink(openAt));
  } else {
    uint32_t cur = cursor_;
    replace(we, 0, mark, cur >= we ? cur + m : cur);
    replace(ws, 0, mark, cursor_ >= ws ? cursor_ + m : cursor_);
  }
  endGroup();
}

void Doc::toggleBold() { toggleWrap("**"); }
void Doc::toggleItalic() { toggleWrap("_"); }

// ---------------------------------------------------------------- navigation

void Doc::setCursor(uint32_t pos) {
  cursor_ = pos > text_.size() ? text_.size() : pos;
  wantX_ = -1;
}

void Doc::normalizeCursor(int dir) {
  if (paras_.empty()) return;
  int i = paraAt(cursor_);
  const Para &pa = paras_[i];
  if (pa.h == 0 && pa.kind == B_BLANK) {
    if (dir > 0 && i + 1 < (int)paras_.size()) cursor_ = paras_[i + 1].start;
    else if (i > 0) cursor_ = paras_[i - 1].start + paras_[i - 1].len;
  }
  i = paraAt(cursor_);
  const Para &q = paras_[i];
  if (markdown_ && q.prefix > 0 && hasPrefixMarker(q.kind) && q.kind != B_HR && cursor_ < q.start + q.prefix) {
    if (dir < 0 && i > 0 && cursor_ == q.start) {
      int j = i - 1;
      while (j > 0 && paras_[j].h == 0) j--;
      cursor_ = paras_[j].start + paras_[j].len;
    } else {
      cursor_ = q.start + q.prefix;
    }
  }
}

void Doc::move(Move m) {
  ensureLayout();
  bool keepX = false;
  int dir = 1;
  int i = paraAt(cursor_);
  switch (m) {
    case LEFT: {
      dir = -1;
      if (cursor_ == 0) break;
      const Para &pa = paras_[i];
      if (cursor_ == pa.start) {
        int j = i - 1;
        while (j > 0 && paras_[j].h == 0) j--;
        cursor_ = paras_[j].start + paras_[j].len;
      } else {
        cursor_--;
        while (cursor_ > 0 && ((uint8_t)text_[cursor_] & 0xC0) == 0x80) cursor_--;
      }
      break;
    }
    case RIGHT: {
      if (cursor_ >= text_.size()) break;
      const Para &pa = paras_[i];
      if (cursor_ == pa.start + pa.len) {
        int j = i + 1;
        while (j < (int)paras_.size() - 1 && paras_[j].h == 0) j++;
        cursor_ = paras_[j].start;
      } else {
        cursor_++;
        while (cursor_ < text_.size() && ((uint8_t)text_[cursor_] & 0xC0) == 0x80) cursor_++;
      }
      break;
    }
    case UP:
    case DOWN: {
      keepX = true;
      dir = m == UP ? -1 : 1;
      const Para &pa = paras_[i];
      uint32_t rel = cursor_ - pa.start;
      int L = lineOf(pa, rel);
      int ti = i, tl = L;
      if (wantX_ < 0) {
        int x, y, lh, asc;
        caretGeom(x, y, lh, asc);
        wantX_ = x - fmt().marginX;
      }
      if (m == UP) {
        if (L > 0) tl = L - 1;
        else {
          int j = i - 1;
          while (j > 0 && paras_[j].h == 0) j--;
          if (j < 0 || (j == 0 && paras_[0].h == 0)) { cursor_ = 0; break; }
          ti = j;
          tl = (int)paras_[j].lines.size() - 1;
        }
      } else {
        if (L + 1 < (int)pa.lines.size()) tl = L + 1;
        else {
          int j = i + 1;
          while (j < (int)paras_.size() - 1 && paras_[j].h == 0) j++;
          if (j >= (int)paras_.size()) { cursor_ = text_.size(); break; }
          ti = j;
          tl = 0;
        }
      }
      prepStyles(ti);
      cursor_ = offAtX(ti, tl, wantX_);
      break;
    }
    case HOME: {
      const Para &pa = paras_[i];
      int L = lineOf(pa, cursor_ - pa.start);
      cursor_ = pa.start + pa.lines[L];
      dir = -1;
      break;
    }
    case END: {
      const Para &pa = paras_[i];
      int L = lineOf(pa, cursor_ - pa.start);
      uint32_t e = (L + 1 < (int)pa.lines.size()) ? pa.lines[L + 1] : pa.len;
      if (L + 1 < (int)pa.lines.size() && e > 0 && text_[pa.start + e - 1] == ' ') e--;
      cursor_ = pa.start + e;
      break;
    }
    case PGUP:
    case PGDN: {
      int lh = lineH(B_BODY);
      int n = max(1, viewH_ / max(1, lh) - 1);
      for (int k = 0; k < n; k++) move(m == PGUP ? UP : DOWN);
      return;
    }
    case WORDL: {
      dir = -1;
      while (cursor_ > 0 && (text_[cursor_ - 1] == ' ' || text_[cursor_ - 1] == '\n')) cursor_--;
      while (cursor_ > 0 && text_[cursor_ - 1] != ' ' && text_[cursor_ - 1] != '\n') cursor_--;
      break;
    }
    case WORDR: {
      while (cursor_ < text_.size() && (text_[cursor_] == ' ' || text_[cursor_] == '\n')) cursor_++;
      while (cursor_ < text_.size() && text_[cursor_] != ' ' && text_[cursor_] != '\n') cursor_++;
      break;
    }
    case DOCSTART: cursor_ = 0; dir = -1; break;
    case DOCEND: cursor_ = text_.size(); break;
  }
  if (!keepX) wantX_ = -1;
  normalizeCursor(dir);
}

// ---------------------------------------------------------------- metrics + layout

fonts::Ref Doc::refFor(Block b) const {
  const DocFormat &f = fmt();
  int lvl = f.size;
  if (b == B_H1) lvl = min(5, lvl + 2);
  else if (b == B_H2) lvl = min(5, lvl + 1);
  return {fonts::faceIndex(f.face), lvl};
}

uint8_t Doc::baseVariant(Block b) const { return (b == B_H1 || b == B_H2 || b == B_H3) ? fonts::V_BOLD : 0; }

int Doc::lineH(Block b) const {
  fonts::Ref r = refFor(b);
  return max(1, fonts::lineHeight(r) * fmt().lineSpacing / 100);
}

int Doc::textW() const { return LCD_W - 2 * fmt().marginX; }

int Doc::adv(const fonts::Ref &r, uint8_t variant, uint32_t cp) const {
  if (cp == '\t') return 4 * fonts::adv(r, variant, ' ');
  return fonts::adv(r, variant, cp);
}

static uint8_t variantOf(uint8_t base, uint8_t st) { return base | ((st & ST_BOLD) ? fonts::V_BOLD : 0) | ((st & ST_ITAL) ? fonts::V_ITAL : 0); }

static bool findDouble(const char *s, int from, int n) {
  for (int j = from; j + 1 < n; j++)
    if (s[j] == '*' && s[j + 1] == '*') return true;
  return false;
}
// A closing underscore must follow a non-space and not sit inside a word (snake_case stays literal).
static bool findUnder(const char *s, int from, int n) {
  for (int j = from + 1; j < n; j++)
    if (s[j] == '_' && s[j - 1] != ' ' && s[j - 1] != '_' && (j + 1 >= n || !isalnum((uint8_t)s[j + 1]))) return true;
  return false;
}

void Doc::prepStyles(int i) {
  Para &pa = paras_[i];
  const char *s = p(pa);
  int n = pa.len;
  sty_.assign(n, 0);
  styFor_ = i;
  bool active = pa.laidActive;
  int pre = pa.prefix;
  if (!active && hasPrefixMarker(pa.kind)) {
    for (int k = 0; k < pre && k < n; k++) sty_[k] = ST_HID;
  }
  if (!markdown_ || pa.kind == B_HR) return;
  // Obsidian-style live preview: a **bold** or *italic* span shows its markers
  // only while the cursor is inside it; everywhere else the markers are hidden
  // and the text is simply bold or italic.
  bool bold = false, ital = false;
  int boldOpen = -1, italOpen = -1;
  uint32_t cur = (active && cursor_ >= pa.start) ? cursor_ - pa.start : 0xFFFFFFFFu;
  auto reveal = [&](int openAt, int openLen, int closeAt, int closeLen) {
    if (cur == 0xFFFFFFFFu) return;
    if (cur >= (uint32_t)openAt && cur <= (uint32_t)(closeAt + closeLen)) {
      for (int q = 0; q < openLen; q++) sty_[openAt + q] &= ~ST_HID;
      for (int q = 0; q < closeLen; q++) sty_[closeAt + q] &= ~ST_HID;
    }
  };
  int k = pre;
  while (k < n) {
    if (s[k] == '*' && k + 1 < n && s[k + 1] == '*' && (bold || findDouble(s, k + 2, n))) {
      bool opening = !bold;
      bold = !bold;
      if (opening) boldOpen = k;
      sty_[k] = sty_[k + 1] = ST_HID | ST_BOLD | (ital ? ST_ITAL : 0);
      if (!opening) reveal(boldOpen, 2, k, 2);
      k += 2;
      continue;
    }
    if (s[k] == '_') {
      bool prevWord = k > 0 && isalnum((uint8_t)s[k - 1]);
      bool nextWord = k + 1 < n && isalnum((uint8_t)s[k + 1]);
      bool closing = ital && k > 0 && s[k - 1] != ' ' && !nextWord;
      bool opening = !ital && !prevWord && k + 1 < n && s[k + 1] != ' ' && s[k + 1] != '_' && findUnder(s, k + 1, n);
      if (closing || opening) {
        ital = !ital;
        if (opening) italOpen = k;
        sty_[k] = ST_HID | (bold ? ST_BOLD : 0) | ST_ITAL;
        if (closing) reveal(italOpen, 1, k, 1);
        k++;
        continue;
      }
    }
    sty_[k] = (bold ? ST_BOLD : 0) | (ital ? ST_ITAL : 0);
    k++;
  }
}

int Doc::measure(const Para &pa, const fonts::Ref &r, uint8_t bv, uint32_t from, uint32_t to) const {
  const char *s = text_.data() + pa.start;
  int n = pa.len, w = 0;
  int k = from;
  while (k < (int)to && k < n) {
    int k0 = k;
    uint32_t cp = dec(s, n, k);
    uint8_t st = k0 < (int)sty_.size() ? sty_[k0] : 0;
    if (st & ST_HID) continue;
    w += adv(r, variantOf(bv, st), cp);
  }
  return w;
}

int Doc::lineX0(const Para &pa, int line, bool active, const fonts::Ref &r) const {
  int x = 0;
  int zero = fonts::adv(r, 0, '0');
  if (!active && isList(pa.kind)) {
    int step = fonts::ascent(r) + 6;
    int gutter = pa.kind == B_NUM ? zero * 3 + 4 : step;
    x = pa.depth * step + gutter;
  }
  if (line == 0 && pa.kind == B_BODY && !pa.suppressIndent && fmt().indentChars) x += fmt().indentChars * fonts::adv(r, 0, 'n');
  return x;
}

int Doc::lineOf(const Para &pa, uint32_t rel) const {
  int L = 0;
  for (int k = 1; k < (int)pa.lines.size(); k++) {
    if (pa.lines[k] <= rel) L = k;
    else break;
  }
  return L;
}

void Doc::layoutPara(int i) {
  Para &pa = paras_[i];
  classify(pa);
  pa.laidActive = (i == act_);
  pa.lines.clear();
  pa.lines.push_back(0);
  pa.padTop = 0;
  Block k = pa.kind;
  if (k == B_BLANK) {
    pa.h = (fmt().tight && !pa.laidActive) ? 0 : lineH(B_BODY);
    return;
  }
  int j = i - 1;
  while (j >= 0 && paras_[j].kind == B_BLANK) j--;
  pa.suppressIndent = (j < 0) || paras_[j].kind != B_BODY;

  fonts::Ref r = refFor(k);
  uint8_t bv = baseVariant(k);
  int lh = lineH(k);
  prepStyles(i);
  const char *s = p(pa);
  int n = pa.len;
  bool active = pa.laidActive;
  int avail = textW();
  int availFirst = avail - lineX0(pa, 0, active, r);
  int availRest = avail - lineX0(pa, 1, active, r);
  int curAvail = availFirst;
  int curW = 0, brk = -1, wAtBrk = 0, lineStart = 0;
  int idx = 0;
  while (idx < n) {
    int i0 = idx;
    uint32_t cp = dec(s, n, idx);
    uint8_t st = sty_[i0];
    int w = (st & ST_HID) ? 0 : adv(r, variantOf(bv, st), cp);
    if (cp == ' ') {
      curW += w;
      brk = idx;
      wAtBrk = curW;
      continue;
    }
    if (curW + w > curAvail && curW > 0) {
      int ns;
      if (brk > lineStart) {
        ns = brk;
        curW -= wAtBrk;
      } else {
        ns = i0;
        curW = 0;
      }
      if (ns < 65000) pa.lines.push_back(ns);
      lineStart = ns;
      curAvail = availRest;
      brk = -1;
      wAtBrk = 0;
    }
    curW += w;
  }
  int top = 0, bottom = 0;
  if (i > 0 && !active) {
    if (k == B_H1) top = lh * 3 / 4;
    else if (k == B_H2) top = lh / 2;
    else if (k == B_H3) top = lh / 3;
    if (paras_[i - 1].kind == B_H1 || paras_[i - 1].kind == B_H2 || paras_[i - 1].kind == B_H3) top = 0;
  }
  if (k == B_H1 || k == B_H2 || k == B_H3) bottom = lh / 4;
  pa.padTop = top;
  pa.h = (int16_t)(top + bottom + (int)pa.lines.size() * lh);
}

void Doc::rebuildY() {
  yTop_.resize(paras_.size());
  int32_t y = 0;
  for (size_t i = 0; i < paras_.size(); i++) {
    yTop_[i] = y;
    y += paras_[i].h;
  }
  totalH_ = y;
  yDirty_ = false;
}

void Doc::ensureLayout() {
  if (paras_.empty()) splitAll();
  int act = paraAt(cursor_);
  if (lastActive_ != act) {
    if (lastActive_ >= 0 && lastActive_ < (int)paras_.size()) paras_[lastActive_].h = -1;
    paras_[act].h = -1;
  }
  // markers inside the active paragraph depend on where the cursor is
  if (act == lastActive_ && cursor_ != lastCursor_) paras_[act].h = -1;
  lastCursor_ = cursor_;
  act_ = act;
  lastActive_ = act;
  bool any = false;
  for (int i = 0; i < (int)paras_.size(); i++) {
    if (paras_[i].h < 0) {
      layoutPara(i);
      any = true;
    }
  }
  if (any || yDirty_) rebuildY();
}

void Doc::relayout() {
  for (auto &pa : paras_) pa.h = -1;
  yDirty_ = true;
}

uint32_t Doc::offAtX(int i, int line, int x) {
  Para &pa = paras_[i];
  fonts::Ref r = refFor(pa.kind);
  uint8_t bv = baseVariant(pa.kind);
  const char *s = p(pa);
  int n = pa.len;
  int x0 = lineX0(pa, line, pa.laidActive, r);
  int target = x - x0;
  uint32_t a = pa.lines[line];
  uint32_t b = (line + 1 < (int)pa.lines.size()) ? pa.lines[line + 1] : pa.len;
  uint32_t maxOff = b;
  if (line + 1 < (int)pa.lines.size() && b > a) {
    maxOff = b - 1;
    while (maxOff > a && ((uint8_t)s[maxOff] & 0xC0) == 0x80) maxOff--;
  }
  int acc = 0;
  int k = a;
  uint32_t res = a;
  while (k < (int)maxOff && k < n) {
    int k0 = k;
    uint32_t cp = dec(s, n, k);
    uint8_t st = sty_[k0];
    int w = (st & ST_HID) ? 0 : adv(r, variantOf(bv, st), cp);
    if (target < acc + w / 2) { res = k0; return pa.start + res; }
    acc += w;
    res = k;
  }
  res = maxOff;
  uint32_t floor = line == 0 ? pa.prefix : 0;
  if (res < floor && markdown_ && hasPrefixMarker(pa.kind)) res = floor;
  return pa.start + res;
}

void Doc::caretGeom(int &x, int &yDoc, int &lh, int &asc) {
  ensureLayout();
  int i = paraAt(cursor_);
  Para &pa = paras_[i];
  Block k = pa.kind;
  fonts::Ref r = refFor(k);
  uint8_t bv = baseVariant(k);
  prepStyles(i);
  uint32_t rel = cursor_ - pa.start;
  int L = lineOf(pa, rel);
  lh = lineH(k);
  asc = fonts::ascent(r);
  x = fmt().marginX + lineX0(pa, L, true, r) + measure(pa, r, bv, pa.lines[L], rel);
  yDoc = yTop_[i] + pa.padTop + L * lh;
}

void Doc::scrollToCursor(int areaH) {
  int x, y, lh, asc;
  caretGeom(x, y, lh, asc);
  int32_t maxS;
  if (S.typewriter) {
    int32_t target = y - areaH * 42 / 100 + lh / 2;
    maxS = max<int32_t>(0, totalH_ - lh * 2);
    scroll_ = constrain(target, 0, maxS);
  } else {
    if (y < scroll_) scroll_ = y - lh;
    else if (y + lh > scroll_ + areaH) scroll_ = y + 2 * lh - areaH;
    maxS = max<int32_t>(0, totalH_ - areaH);
    scroll_ = constrain(scroll_, 0, maxS);
  }
}

// ---------------------------------------------------------------- drawing

void Doc::drawPara(int i, int yScreen, int top, int bottom) {
  Para &pa = paras_[i];
  if (pa.h <= 0 || pa.kind == B_BLANK) return;
  Block k = pa.kind;
  bool active = pa.laidActive;
  fonts::Ref r = refFor(k);
  uint8_t bv = baseVariant(k);
  int lh = lineH(k);
  int raw = fonts::lineHeight(r);
  int asc = fonts::ascent(r);
  prepStyles(i);
  const char *s = p(pa);
  int n = pa.len;
  int mx = fmt().marginX;
  int nl = pa.lines.size();
  U8G2 &u = gfx::u();
  bool italFallback = !fonts::hasItalic(r);
  for (int L = 0; L < nl; L++) {
    int ly = yScreen + pa.padTop + L * lh;
    if (ly + lh < top || ly > bottom) continue;
    int base = ly + (lh - raw) / 2 + asc;
    int a = pa.lines[L];
    int b = (L + 1 < nl) ? pa.lines[L + 1] : n;
    int x = mx + lineX0(pa, L, active, r);
    if (!active && k == B_H1 && fmt().h1Center) {
      int e = b;
      while (e > a && s[e - 1] == ' ') e--;
      int w = measure(pa, r, bv, a, e);
      x = mx + max(0, (textW() - w) / 2);
    }
    int startX = x;
    int idx = a;
    while (idx < b) {
      int i0 = idx;
      uint32_t cp = dec(s, n, idx);
      uint8_t st = sty_[i0];
      if (st & ST_HID) continue;
      uint8_t v = variantOf(bv, st);
      int w = adv(r, v, cp);
      fonts::draw(r, v, x, base, cp);
      if (italFallback && (v & fonts::V_ITAL) && cp != ' ') u.drawHLine(x, base + 1, w);
      x += w;
    }
    if (pa.checked && k == B_TODO && !active) u.drawHLine(startX, base - asc / 3, x - startX);
    if (L == 0 && !active && isList(k)) {
      int step = asc + 6;
      int gx = mx + pa.depth * step;
      if (k == B_BULLET) {
        int cy = base - asc / 2 + 1;
        if (pa.depth == 0) u.drawDisc(gx + 4, cy, max(2, asc / 7));
        else u.drawCircle(gx + 4, cy, max(2, asc / 7));
      } else if (k == B_TODO) {
        int bs = max(8, asc * 8 / 10);
        int by = base - bs + 1;
        u.drawFrame(gx, by, bs, bs);
        if (pa.checked) {
          u.drawLine(gx + 2, by + 2, gx + bs - 3, by + bs - 3);
          u.drawLine(gx + 2, by + bs - 3, gx + bs - 3, by + 2);
        }
      } else {
        int sp = pa.depth ? 2 : 0;
        int px = gx;
        for (int q = sp; q < pa.prefix - 1; q++) {
          fonts::draw(r, 0, px, base, (uint8_t)s[q]);
          px += adv(r, 0, (uint8_t)s[q]);
        }
      }
    }
    if (k == B_HR && !active) {
      u.drawHLine(mx, ly + lh / 2, textW());
      u.drawHLine(mx, ly + lh / 2 + 1, textW());
    }
  }
}

void Doc::render(int top, int height) {
  viewH_ = height;
  ensureLayout();
  scrollToCursor(height);
  U8G2 &u = gfx::u();
  u.setClipWindow(0, top, LCD_W, top + height);
  int first = 0;
  {
    int lo = 0, hi = (int)paras_.size() - 1;
    while (lo < hi) {
      int mid = (lo + hi + 1) / 2;
      if (yTop_[mid] <= scroll_) lo = mid;
      else hi = mid - 1;
    }
    first = lo;
  }
  for (int i = first; i < (int)paras_.size(); i++) {
    int ys = yTop_[i] - scroll_ + top;
    if (ys > top + height) break;
    drawPara(i, ys, top, top + height);
  }
  int cx, cy, lh, asc;
  caretGeom(cx, cy, lh, asc);
  int sy = cy - scroll_ + top;
  u.setDrawColor(gfx::ink());
  u.drawVLine(cx, sy + 1, lh - 2);
  u.drawVLine(cx + 1, sy + 1, lh - 2);
  u.setMaxClipWindow();
}

int Doc::words() {
  if (!wordsDirty_) return wordsCache_;
  int w = 0;
  bool in = false, alnum = false;
  for (char c : text_) {
    bool sp = c == ' ' || c == '\n' || c == '\t';
    if (sp) {
      if (in && alnum) w++;
      in = alnum = false;
    } else {
      in = true;
      if (isalnum((uint8_t)c) || (uint8_t)c >= 0x80) alnum = true;
    }
  }
  if (in && alnum) w++;
  wordsCache_ = w;
  wordsDirty_ = false;
  return w;
}

int Doc::percent() { return text_.empty() ? 0 : (int)((uint64_t)cursor_ * 100 / text_.size()); }

Block Doc::blockAtCursor() {
  if (paras_.empty()) return B_BODY;
  return paras_[paraAt(cursor_)].kind;
}
