#include "fonts.h"
#include <FS.h>
#include <map>
#include <vector>
#include "gfx.h"
#include "layout.h"
#include "storage.h"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_malloc(x, u) ps_malloc(x)
#define STBTT_free(x, u) free(x)
#include "stb_truetype.h"

// TTFs linked into the firmware image (see board_build.embed_files)
extern const uint8_t kAveriaReg[] asm("_binary_fonts_AveriaSerifLibre_Regular_ttf_start");
extern const uint8_t kAveriaBold[] asm("_binary_fonts_AveriaSerifLibre_Bold_ttf_start");
extern const uint8_t kAveriaItal[] asm("_binary_fonts_AveriaSerifLibre_Italic_ttf_start");
extern const uint8_t kAveriaBoldItal[] asm("_binary_fonts_AveriaSerifLibre_BoldItalic_ttf_start");
extern const uint8_t kQuickReg[] asm("_binary_fonts_Quicksand_Regular_ttf_start");
extern const uint8_t kQuickBold[] asm("_binary_fonts_Quicksand_Bold_ttf_start");

namespace fonts {

struct Builtin {
  const char *name;
  const uint8_t *reg[LEVELS];
  const uint8_t *bold[LEVELS];
};

static const Builtin kBuiltin[] = {
    {"Serif",
     {u8g2_font_ncenR08_tf, u8g2_font_ncenR10_tf, u8g2_font_ncenR12_tf, u8g2_font_ncenR14_tf, u8g2_font_ncenR18_tf, u8g2_font_ncenR24_tf},
     {u8g2_font_ncenB08_tf, u8g2_font_ncenB10_tf, u8g2_font_ncenB12_tf, u8g2_font_ncenB14_tf, u8g2_font_ncenB18_tf, u8g2_font_ncenB24_tf}},
    {"Sans",
     {u8g2_font_helvR08_tf, u8g2_font_helvR10_tf, u8g2_font_helvR12_tf, u8g2_font_helvR14_tf, u8g2_font_helvR18_tf, u8g2_font_helvR24_tf},
     {u8g2_font_helvB08_tf, u8g2_font_helvB10_tf, u8g2_font_helvB12_tf, u8g2_font_helvB14_tf, u8g2_font_helvB18_tf, u8g2_font_helvB24_tf}},
    {"Mono",
     {u8g2_font_courR08_tf, u8g2_font_courR10_tf, u8g2_font_courR12_tf, u8g2_font_courR14_tf, u8g2_font_courR18_tf, u8g2_font_courR24_tf},
     {u8g2_font_courB08_tf, u8g2_font_courB10_tf, u8g2_font_courB12_tf, u8g2_font_courB14_tf, u8g2_font_courB18_tf, u8g2_font_courB24_tf}},
};
static const int kNumBuiltin = sizeof(kBuiltin) / sizeof(kBuiltin[0]);

static const int kTtfPx[LEVELS] = {D(12), D(14), D(16), D(19), D(24), D(32)};
static const int kThreshold = 96;  // coverage needed to light a pixel

// ---- TTF families ---------------------------------------------------------------
struct StyleFile {
  const uint8_t *data = nullptr;
  bool owned = false;  // true when we malloc'd it (SD)
  String path;         // SD source
  bool ready = false;
  bool failed = false;
  stbtt_fontinfo info;
  bool present() const { return data != nullptr || path.length() > 0; }
};
struct TtfFace {
  String name;
  StyleFile f[4];  // indexed by variant bits: 0 reg, 1 bold, 2 italic, 3 bold+italic
};
static std::vector<TtfFace *> ttf;
static int embeddedCount = 0;

struct Pick {
  StyleFile *sf = nullptr;
  int idx = 0;
  bool synthBold = false;
  bool synthItal = false;
};

static bool prepare(StyleFile &s) {
  if (s.ready) return true;
  if (s.failed) return false;
  if (!s.data && s.path.length()) {
    fs::File fh = storage::fs().open(s.path, "r");
    if (!fh) { s.failed = true; return false; }
    size_t n = fh.size();
    uint8_t *buf = (uint8_t *)ps_malloc(n);
    if (!buf || fh.read(buf, n) != n) {
      if (buf) free(buf);
      fh.close();
      s.failed = true;
      return false;
    }
    fh.close();
    s.data = buf;
    s.owned = true;
  }
  if (!s.data || !stbtt_InitFont(&s.info, s.data, stbtt_GetFontOffsetForIndex(s.data, 0))) {
    if (s.owned) free((void *)s.data);
    s.data = nullptr;
    s.failed = true;
    return false;
  }
  s.ready = true;
  return true;
}

// Choose the best file for a style and note what has to be faked.
static Pick pick(TtfFace *f, uint8_t variant) {
  bool wantB = variant & V_BOLD, wantI = variant & V_ITAL;
  int want = (wantB ? 1 : 0) | (wantI ? 2 : 0);
  Pick p;
  auto use = [&](int i, bool sb, bool si) -> bool {
    if (f->f[i].present() && prepare(f->f[i])) {
      p.sf = &f->f[i];
      p.idx = i;
      p.synthBold = sb;
      p.synthItal = si;
      return true;
    }
    return false;
  };
  if (use(want, false, false)) return p;
  if (wantB && wantI) {
    if (use(2, true, false)) return p;   // italic + faux bold
    if (use(1, false, true)) return p;   // bold + faux slant
  }
  if (use(wantB ? 1 : 0, false, wantI)) return p;
  if (use(0, wantB, wantI)) return p;
  return p;
}

struct Glyph {
  uint64_t key = 0;  // 0 = empty
  int16_t xoff = 0, yoff = 0;
  uint8_t w = 0, h = 0;
  int16_t adv = 0;
  uint8_t *bits = nullptr;
};
static const int kGlyphSlots = 384;
static Glyph glyphs[kGlyphSlots];

static void clearGlyphs() {
  for (auto &g : glyphs) {
    if (g.bits) free(g.bits);
    g = Glyph();
  }
}

static void addEmbedded(const char *name, const uint8_t *reg, const uint8_t *bold, const uint8_t *ital, const uint8_t *bi) {
  TtfFace *f = new TtfFace();
  f->name = name;
  f->f[0].data = reg;
  f->f[1].data = bold;
  f->f[2].data = ital;
  f->f[3].data = bi;
  ttf.push_back(f);
  embeddedCount++;
}

// "Name-BoldItalic.ttf" -> family "Name", variant 3. Returns -1 for styles we do not use.
static int parseStyle(String base, String &family) {
  family = base;
  String lower = base;
  lower.toLowerCase();
  if (lower.indexOf("variable") >= 0) return -1;
  int dash = base.lastIndexOf('-');
  if (dash < 0) return 0;
  String st = lower.substring(dash + 1);
  family = base.substring(0, dash);
  st.replace(" ", "");
  if (st == "regular" || st == "book" || st == "roman") return 0;
  if (st == "bold") return 1;
  if (st == "italic" || st == "oblique") return 2;
  if (st == "bolditalic" || st == "boldoblique") return 3;
  return -1;
}

void rescan() {
  clearGlyphs();
  // drop SD families, keep the embedded ones
  while ((int)ttf.size() > embeddedCount) {
    TtfFace *f = ttf.back();
    for (auto &s : f->f)
      if (s.owned && s.data) free((void *)s.data);
    delete f;
    ttf.pop_back();
  }
  if (!storage::ready()) return;
  fs::File dir = storage::fs().open(FONT_DIR);
  if (!dir || !dir.isDirectory()) return;
  std::map<String, TtfFace *> fam;
  for (fs::File e = dir.openNextFile(); e; e = dir.openNextFile()) {
    if (e.isDirectory()) continue;
    String n = e.name();
    String lower = n;
    lower.toLowerCase();
    if (!lower.endsWith(".ttf")) continue;
    String family;
    int v = parseStyle(n.substring(0, n.length() - 4), family);
    if (v < 0) continue;
    bool dup = false;
    for (int i = 0; i < embeddedCount; i++)
      if (ttf[i]->name.equalsIgnoreCase(family)) dup = true;
    if (dup) continue;
    TtfFace *&f = fam[family];
    if (!f) {
      if (fam.size() > 8) { fam.erase(family); break; }
      f = new TtfFace();
      f->name = family;
    }
    f->f[v].path = String(FONT_DIR) + "/" + n;
  }
  for (auto &kv : fam) {
    if (kv.second && kv.second->f[0].present()) ttf.push_back(kv.second);
    else if (kv.second) delete kv.second;
  }
}

void begin() {
  if (embeddedCount == 0) {
    addEmbedded("Averia Serif", kAveriaReg, kAveriaBold, kAveriaItal, kAveriaBoldItal);
    addEmbedded("Quicksand", kQuickReg, kQuickBold, nullptr, nullptr);
  }
  rescan();
}

int faceCount() { return kNumBuiltin + (int)ttf.size(); }
const char *faceName(int i) {
  if (i < 0) i = 0;
  if (i < kNumBuiltin) return kBuiltin[i].name;
  i -= kNumBuiltin;
  if (i < (int)ttf.size()) return ttf[i]->name.c_str();
  return kBuiltin[0].name;
}
int faceIndex(const char *name) {
  for (int i = 0; i < faceCount(); i++)
    if (strcasecmp(faceName(i), name) == 0) return i;
  return 0;
}
bool hasItalic(const Ref &) { return true; }

// ---- character folding ------------------------------------------------------------
// Bitmap fonts only cover Latin-1, so map the usual typographic characters
// (smart quotes, dashes, ellipsis, nbsp) that other editors produce.
static uint32_t fold(uint32_t cp) {
  switch (cp) {
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: return 0x27;
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: return '"';
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: return '-';
    case 0x2022: case 0x25CF: return '*';
    case 0x2026: return '.';
    case 0x00A0: case 0x2009: case 0x202F: case 0x2002: case 0x2003: return ' ';
    default: return cp;
  }
}

// ---- ttf glyphs -----------------------------------------------------------------------
static Glyph *ttfGlyph(int face, int fileIdx, StyleFile *sf, int level, uint32_t cp) {
  uint64_t key = ((uint64_t)(face + 1) << 40) | ((uint64_t)fileIdx << 36) | ((uint64_t)level << 32) | cp | 0x8000000000000000ULL;
  Glyph &g = glyphs[((uint32_t)key ^ (uint32_t)(key >> 32)) * 2654435761u % kGlyphSlots];
  if (g.key == key) return &g;
  if (g.bits) { free(g.bits); g.bits = nullptr; }
  float scale = stbtt_ScaleForMappingEmToPixels(&sf->info, (float)kTtfPx[level]);
  int advW, lsb;
  stbtt_GetCodepointHMetrics(&sf->info, cp, &advW, &lsb);
  g.adv = (int16_t)(advW * scale + 0.5f);
  int w = 0, h = 0, xo = 0, yo = 0;
  unsigned char *bm = stbtt_GetCodepointBitmap(&sf->info, scale, scale, cp, &w, &h, &xo, &yo);
  g.w = g.h = 0;
  if (bm) {
    g.w = (uint8_t)min(w, 255);
    g.h = (uint8_t)min(h, 255);
    g.xoff = xo;
    g.yoff = yo;
    g.bits = (uint8_t *)ps_malloc((size_t)g.w * g.h);
    if (g.bits) {
      for (int r = 0; r < g.h; r++)
        for (int c = 0; c < g.w; c++) g.bits[r * g.w + c] = bm[r * w + c] >= kThreshold;
    }
    free(bm);
  }
  g.key = key;
  return &g;
}

static TtfFace *ttfOf(const Ref &r) {
  int idx = r.face - kNumBuiltin;
  if (idx < 0 || idx >= (int)ttf.size()) return nullptr;
  return ttf[idx];
}

// ---- sheared bitmap glyphs (italic for the built-in fonts) ---------------------------------
// Scratch canvas for one glyph: 56x40 design px, scaled for bigger fonts.
static const int kTmpTilesW = (D(56) + 7) / 8, kTmpTilesH = (D(40) + 7) / 8;
static const int kTmpW = kTmpTilesW * 8, kTmpBase = D(30), kTmpOx = D(4);
static u8g2_t tmpU;
static uint8_t tmpBuf[kTmpTilesW * 8 * kTmpTilesH];
static bool tmpReady = false;

static uint8_t tmpDisplayCb(u8x8_t *u8x8, uint8_t msg, uint8_t, void *) {
  if (msg == U8X8_MSG_DISPLAY_SETUP_MEMORY) {
    static const u8x8_display_info_t info = {0, 1, 0, 0, 0, 0, 0, 0, 1000000, 0, 4, 0, 0, kTmpTilesW, kTmpTilesH, 0, 0, kTmpTilesW * 8, kTmpTilesH * 8};
    u8x8_d_helper_display_setup_memory(u8x8, &info);
  }
  return 1;
}

static void initTmp() {
  u8x8_Setup(u8g2_GetU8x8(&tmpU), tmpDisplayCb, u8x8_dummy_cb, u8x8_dummy_cb, u8x8_dummy_cb);
  u8g2_SetupBuffer(&tmpU, tmpBuf, kTmpTilesH, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
  u8g2_SetMaxClipWindow(&tmpU);
  u8g2_SetFontMode(&tmpU, 1);
  u8g2_SetDrawColor(&tmpU, 1);
  tmpReady = true;
}

static void drawSheared(const uint8_t *font, int x, int baseline, uint32_t cp) {
  if (!tmpReady) initTmp();
  memset(tmpBuf, 0, sizeof(tmpBuf));
  u8g2_SetFont(&tmpU, font);
  u8g2_DrawGlyph(&tmpU, kTmpOx, kTmpBase, cp);
  U8G2 &out = gfx::u();
  for (int page = 0; page < kTmpTilesH; page++) {
    for (int col = 0; col < kTmpW; col++) {
      uint8_t b = tmpBuf[page * kTmpW + col];
      if (!b) continue;
      for (int bit = 0; bit < 8; bit++) {
        if (!(b & (1 << bit))) continue;
        int py = page * 8 + bit;
        int up = kTmpBase - py;  // rows above the baseline
        int dx = (up * 3) / 14;
        out.drawPixel(x + col - kTmpOx + dx, baseline + (py - kTmpBase));
      }
    }
  }
}

// ---- built-ins ---------------------------------------------------------------------------------
static const uint8_t *builtinFont(const Ref &r, uint8_t variant) {
  const Builtin &b = kBuiltin[r.face];
  return SF((variant & V_BOLD) ? b.bold[r.level] : b.reg[r.level]);
}

struct AdvTable {
  const uint8_t *font = nullptr;
  uint8_t adv[224];  // cp 32..255
};
static std::vector<AdvTable *> advTables;

static AdvTable *tableFor(const uint8_t *font) {
  for (auto *t : advTables)
    if (t->font == font) return t;
  if (advTables.size() >= 16) {
    delete advTables.front();
    advTables.erase(advTables.begin());
  }
  AdvTable *t = new AdvTable();
  t->font = font;
  U8G2 &g = gfx::u();
  g.setFont(font);
  for (int c = 32; c < 256; c++) {
    int w = u8g2_GetGlyphWidth(g.getU8g2(), c);
    t->adv[c - 32] = (uint8_t)(w < 0 ? 0 : w);
  }
  advTables.push_back(t);
  return t;
}

int adv(const Ref &r, uint8_t variant, uint32_t cp) {
  if (cp < 32) return 0;
  if (r.face < kNumBuiltin) {
    cp = fold(cp);
    if (cp > 255) return 0;
    return tableFor(builtinFont(r, variant))->adv[cp - 32];
  }
  TtfFace *f = ttfOf(r);
  if (!f) return 0;
  Pick p = pick(f, variant);
  if (!p.sf) return 0;
  Glyph *g = ttfGlyph(r.face, p.idx, p.sf, r.level, cp);
  return g->adv + (p.synthBold ? 1 : 0);
}

void draw(const Ref &r, uint8_t variant, int x, int baseline, uint32_t cp) {
  if (cp < 33) return;
  U8G2 &u = gfx::u();
  if (r.face < kNumBuiltin) {
    cp = fold(cp);
    if (cp > 255) return;
    const uint8_t *font = builtinFont(r, variant);
    if (variant & V_ITAL) {
      drawSheared(font, x, baseline, cp);
    } else {
      u.setFont(font);
      u.drawGlyph(x, baseline, cp);
    }
    return;
  }
  TtfFace *f = ttfOf(r);
  if (!f) return;
  Pick p = pick(f, variant);
  if (!p.sf) return;
  Glyph *g = ttfGlyph(r.face, p.idx, p.sf, r.level, cp);
  if (!g || !g->bits) return;
  for (int row = 0; row < g->h; row++) {
    int py = baseline + g->yoff + row;
    int shear = p.synthItal ? (-(g->yoff + row)) / 4 : 0;
    for (int col = 0; col < g->w; col++) {
      if (!g->bits[row * g->w + col]) continue;
      int px = x + g->xoff + col + shear;
      u.drawPixel(px, py);
      if (p.synthBold) u.drawPixel(px + 1, py);
    }
  }
}

int ascent(const Ref &r) {
  if (r.face < kNumBuiltin) {
    gfx::u().setFont(SF(kBuiltin[r.face].reg[r.level]));
    return gfx::u().getAscent();
  }
  TtfFace *f = ttfOf(r);
  Pick p = f ? pick(f, 0) : Pick();
  if (!p.sf) return kTtfPx[r.level];
  int a, d, l;
  stbtt_GetFontVMetrics(&p.sf->info, &a, &d, &l);
  return (int)(a * stbtt_ScaleForMappingEmToPixels(&p.sf->info, (float)kTtfPx[r.level]) + 0.5f);
}

int lineHeight(const Ref &r) {
  if (r.face < kNumBuiltin) {
    gfx::u().setFont(SF(kBuiltin[r.face].reg[r.level]));
    return gfx::u().getAscent() - gfx::u().getDescent() + 1;
  }
  TtfFace *f = ttfOf(r);
  Pick p = f ? pick(f, 0) : Pick();
  if (!p.sf) return kTtfPx[r.level] + 4;
  int a, d, l;
  stbtt_GetFontVMetrics(&p.sf->info, &a, &d, &l);
  float s = stbtt_ScaleForMappingEmToPixels(&p.sf->info, (float)kTtfPx[r.level]);
  return (int)((a - d + l) * s + 0.5f);
}

}  // namespace fonts
