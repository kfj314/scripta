#pragma once
#include <Arduino.h>
#include <string>
#include <vector>
#include "fonts.h"
#include "settings.h"

enum Block : uint8_t { B_BODY, B_H1, B_H2, B_H3, B_BULLET, B_NUM, B_TODO, B_HR, B_BLANK };

// A text document plus everything needed to edit it live: paragraph index,
// wrapped layout, markdown styling, undo history, cursor and scrolling.
class Doc {
 public:
  enum Move { LEFT, RIGHT, UP, DOWN, HOME, END, PGUP, PGDN, WORDL, WORDR, DOCSTART, DOCEND };

  bool load(const String &path, DocType type);
  bool reload();  // re-read from storage, keeping the cursor as close as possible
  bool save();
  void closeDoc();

  const String &path() const { return path_; }
  DocType type() const { return type_; }
  void setType(DocType t);
  bool dirty() const { return dirty_; }
  uint32_t lastEditMs() const { return lastEdit_; }
  bool markdown() const { return markdown_; }
  bool open() const { return path_.length() > 0; }

  // editing
  void typeChar(uint32_t cp);
  void typeText(const std::string &s);
  void enter();
  void backspace(bool word);
  void del();
  void tab(bool shift);
  void undo();
  void redo();

  // navigation
  void move(Move m);
  void setCursor(uint32_t pos);
  uint32_t cursor() const { return cursor_; }
  size_t size() const { return text_.size(); }

  // formatting
  void heading(int level);
  void toggleBullet();
  void toggleNumbered();
  void toggleCheckbox();
  void toggleBold();
  void toggleItalic();

  // view
  void relayout();  // fonts/margins/format changed
  void render(int top, int height);
  int words();
  int wordsAtOpen() const { return openWords_; }
  int percent();
  Block blockAtCursor();

 private:
  struct Para {
    uint32_t start = 0, len = 0;
    Block kind = B_BODY;
    uint8_t depth = 0;
    uint8_t checked = 0;
    uint16_t prefix = 0;
    bool laidActive = false;
    bool suppressIndent = false;
    int16_t h = -1;  // -1 = needs layout
    int16_t padTop = 0;
    std::vector<uint16_t> lines;  // start offset of each visual line
  };
  struct Op {
    uint32_t pos;
    std::string del, ins;
    uint32_t curBefore, curAfter;
    uint32_t grp;
    uint32_t t;
    bool typing;
  };

  // content
  std::string text_;
  String path_;
  DocType type_ = T_NOTE;
  bool markdown_ = true;
  bool dirty_ = false;
  uint32_t lastEdit_ = 0;
  uint32_t cursor_ = 0;
  int wantX_ = -1;
  int openWords_ = 0;
  int wordsCache_ = 0;
  bool wordsDirty_ = true;

  // layout
  std::vector<Para> paras_;
  std::vector<int32_t> yTop_;
  bool yDirty_ = true;
  int32_t totalH_ = 0;
  int32_t scroll_ = 0;
  int lastActive_ = -1;
  int act_ = -1;
  uint32_t lastCursor_ = 0;
  int viewH_ = 200;
  std::vector<uint8_t> sty_;
  int styFor_ = -1;

  // undo
  std::vector<Op> undo_, redo_;
  uint32_t grpCounter_ = 0;
  uint32_t curGrp_ = 0;
  bool inGroup_ = false;
  size_t undoBytes_ = 0;

  const DocFormat &fmt() const { return S.fmt[type_]; }
  const char *p(const Para &pa) const { return text_.data() + pa.start; }

  void splitAll();
  int paraAt(uint32_t off) const;
  void classify(Para &pa) const;
  void replace(uint32_t pos, uint32_t del, const std::string &ins, uint32_t curAfter, bool coalesce = false);
  void applyRaw(uint32_t pos, uint32_t del, const std::string &ins);
  void beginGroup();
  void endGroup();
  void normalizeCursor(int dir);
  void ensureLayout();
  void layoutPara(int i);
  void rebuildY();
  fonts::Ref refFor(Block b) const;
  uint8_t baseVariant(Block b) const;
  int lineH(Block b) const;
  int textW() const;
  void prepStyles(int i);
  int adv(const fonts::Ref &r, uint8_t variant, uint32_t cp) const;
  int measure(const Para &pa, const fonts::Ref &r, uint8_t bv, uint32_t from, uint32_t to) const;
  int lineX0(const Para &pa, int line, bool active, const fonts::Ref &r) const;
  int lineOf(const Para &pa, uint32_t rel) const;
  uint32_t offAtX(int i, int line, int x);
  void caretGeom(int &x, int &yDoc, int &lh, int &asc);
  void scrollToCursor(int areaH);
  void renumber(int i);
  void renumberJoin(int i);
  void replacePrefix(int i, const std::string &np);
  void wordBounds(uint32_t &ws, uint32_t &we) const;
  void toggleWrap(const char *mark);
  void drawPara(int i, int yScreen, int top, int bottom);
  bool isCollapsedBlank(int i) const;
  void touch();
};
