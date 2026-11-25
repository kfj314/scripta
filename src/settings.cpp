#include "settings.h"
#include <Preferences.h>

Settings S;
static const uint32_t kVersion = 0x53430001;

static void fmt(DocFormat &f, const char *face, uint8_t size, uint8_t indent, uint8_t spacing, bool tight, bool blank, bool center) {
  memset(&f, 0, sizeof(f));
  strlcpy(f.face, face, sizeof(f.face));
  f.size = size;
  f.indentChars = indent;
  f.lineSpacing = spacing;
  f.tight = tight;
  f.blankLines = blank;
  f.h1Center = center;
  f.marginX = 14;
}

void settingsDefaults() {
  memset(&S, 0, sizeof(S));
  S.version = kVersion;
  //   face     size ind sp  tight blank center
  fmt(S.fmt[T_NOVEL],   "Serif", 3, 2, 125, true,  true,  true);
  fmt(S.fmt[T_NOTE],    "Sans",  2, 0, 115, false, true,  false);
  fmt(S.fmt[T_JOURNAL], "Serif", 2, 0, 125, false, true,  false);
  fmt(S.fmt[T_OUTLINE], "Sans",  2, 0, 115, false, false, false);
  fmt(S.fmt[T_TODO],    "Sans",  2, 0, 120, false, false, false);
  fmt(S.fmt[T_PLAIN],   "Mono",  2, 0, 110, false, false, false);
  S.showStatus = 1;
  S.typewriter = 1;
  S.autosaveSec = 10;
  S.sleepMin = 15;
  S.autoSyncMin = 10;
  strlcpy(S.syncRoot, "/", sizeof(S.syncRoot));
  strlcpy(S.tz, "UTC0", sizeof(S.tz));
  strlcpy(S.ntp, "pool.ntp.org", sizeof(S.ntp));
}

void settingsLoad() {
  settingsDefaults();
  Preferences p;
  if (!p.begin("scripta", true)) return;
  size_t len = p.getBytesLength("cfg");
  // Accept the current layout, or an older one that ends before the appended fields.
  if (len == sizeof(Settings) || (len >= offsetof(Settings, kbs) && len < sizeof(Settings))) {
    Settings tmp;
    memset(&tmp, 0, sizeof(tmp));
    p.getBytes("cfg", &tmp, len);
    if (tmp.version == kVersion) S = tmp;
  }
  p.end();
  if (S.legacyKbAddr[0] && !S.kbs[0].addr[0]) {
    strlcpy(S.kbs[0].addr, S.legacyKbAddr, sizeof(S.kbs[0].addr));
    S.kbs[0].type = S.legacyKbType;
  }
  S.legacyKbAddr[0] = 0;
}

void settingsSave() {
  Preferences p;
  if (!p.begin("scripta", false)) return;
  p.putBytes("cfg", &S, sizeof(Settings));
  p.end();
}

const char *typeName(DocType t) {
  static const char *n[] = {"Novel chapter", "Note", "Journal entry", "Outline / list", "To-do list", "Plain text"};
  return n[t < T_COUNT ? t : T_NOTE];
}
const char *typeBlurb(DocType t) {
  static const char *n[] = {"Serif, indented paragraphs, tight", "Sans, spaced paragraphs", "Dated entry, serif", "Bulleted outline", "Checkbox list", "Monospace, no markup"};
  return n[t < T_COUNT ? t : T_NOTE];
}
