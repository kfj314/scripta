#include <WiFi.h>
#include "sync.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <map>
#include <set>
#include "config.h"
#include "meta.h"
#include "settings.h"
#include "storage.h"

namespace davsync {

struct Remote {
  String stamp;
  uint32_t size = 0;
  bool dir = false;
};
struct State {
  uint32_t crc = 0;
  String stamp;
};

static const char *kState = META_DIR "/sync.tsv";
static void (*say)(const String &) = nullptr;
static String baseUrl;       // no trailing slash
static String baseHrefPath;  // path part of baseUrl, no trailing slash
static String lastError;

bool configured() { return S.davUrl[0] != 0; }

// ---- url helpers -------------------------------------------------------------
static String encodePath(const String &rel) {
  String o;
  for (size_t i = 0; i < rel.length(); i++) {
    char c = rel[i];
    if (isalnum((uint8_t)c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') o += c;
    else {
      char b[4];
      snprintf(b, sizeof(b), "%%%02X", (uint8_t)c);
      o += b;
    }
  }
  return o;
}
static String decodePath(const String &s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    if (s[i] == '%' && i + 2 < s.length() + 0 && isxdigit((uint8_t)s[i + 1]) && isxdigit((uint8_t)s[i + 2])) {
      o += (char)strtol(s.substring(i + 1, i + 3).c_str(), nullptr, 16);
      i += 2;
    } else o += s[i];
  }
  return o;
}

static String urlFor(const String &rel) { return baseUrl + encodePath(rel); }

// ---- http ---------------------------------------------------------------------
struct Conn {
  WiFiClient plain;
  WiFiClientSecure tls;
  HTTPClient http;
  bool begin(const String &url) {
    http.setReuse(false);
    http.setTimeout(15000);
    http.setConnectTimeout(8000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    bool ok;
    if (url.startsWith("https://")) {
      tls.setInsecure();  // LAN server; certificate is not verified
      ok = http.begin(tls, url);
    } else {
      ok = http.begin(plain, url);
    }
    if (ok && S.davUser[0]) http.setAuthorization(S.davUser, S.davPass);
    return ok;
  }
};

static int request(const char *method, const String &rel, const String &body, const char *depth, String *resp) {
  Conn c;
  if (!c.begin(urlFor(rel))) return -1;
  if (depth) c.http.addHeader("Depth", depth);
  if (body.length()) c.http.addHeader("Content-Type", "application/xml");
  int code = c.http.sendRequest(method, (uint8_t *)body.c_str(), body.length());
  if (resp && code > 0) {
    if (c.http.getSize() > 2 * 1024 * 1024) { c.http.end(); return -2; }
    *resp = c.http.getString();
  }
  c.http.end();
  return code;
}

// ---- tiny XML scan ---------------------------------------------------------------
// Finds the text of the first element whose local name matches, within [from, to).
static bool tagText(const String &x, const char *local, int from, int to, String &out, int *endPos = nullptr) {
  int i = from;
  size_t ln = strlen(local);
  while (i < to) {
    int lt = x.indexOf('<', i);
    if (lt < 0 || lt >= to) return false;
    int gt = x.indexOf('>', lt);
    if (gt < 0) return false;
    if (x[lt + 1] != '/' && x[lt + 1] != '?') {
      int nameStart = lt + 1;
      int colon = x.indexOf(':', nameStart);
      if (colon > 0 && colon < gt) nameStart = colon + 1;
      int nameEnd = nameStart;
      while (nameEnd < gt && x[nameEnd] != ' ' && x[nameEnd] != '/' && x[nameEnd] != '>') nameEnd++;
      if ((size_t)(nameEnd - nameStart) == ln && strncasecmp(x.c_str() + nameStart, local, ln) == 0) {
        if (x[gt - 1] == '/') { out = ""; if (endPos) *endPos = gt; return true; }
        int close = x.indexOf('<', gt + 1);
        if (close < 0) return false;
        out = x.substring(gt + 1, close);
        if (endPos) *endPos = close;
        return true;
      }
    }
    i = gt + 1;
  }
  return false;
}

static bool hasTag(const String &x, const char *local, int from, int to) {
  String dummy;
  return tagText(x, local, from, to, dummy);
}

static String xmlUnescape(String s) {
  s.replace("&amp;", "&");
  s.replace("&lt;", "<");
  s.replace("&gt;", ">");
  s.replace("&quot;", "\"");
  s.replace("&apos;", "'");
  return s;
}

// Parse a multistatus body; `selfRel` is the directory that was listed.
static void parseListing(const String &x, const String &selfRel, std::map<String, Remote> &out) {
  int pos = 0;
  while (true) {
    int a = -1;
    // find "<...response>" opening (skip closing tags)
    int i = pos;
    while (true) {
      int lt = x.indexOf('<', i);
      if (lt < 0) break;
      int gt = x.indexOf('>', lt);
      if (gt < 0) break;
      if (x[lt + 1] != '/') {
        int ns = lt + 1;
        int colon = x.indexOf(':', ns);
        if (colon > 0 && colon < gt) ns = colon + 1;
        if (strncasecmp(x.c_str() + ns, "response", 8) == 0 && (x[ns + 8] == '>' || x[ns + 8] == ' ')) { a = lt; break; }
      }
      i = gt + 1;
    }
    if (a < 0) break;
    // end: next response opening or end of string
    int nextA = -1;
    int j = x.indexOf('>', a) + 1;
    while (true) {
      int lt = x.indexOf('<', j);
      if (lt < 0) break;
      int gt = x.indexOf('>', lt);
      if (gt < 0) break;
      if (x[lt + 1] != '/') {
        int ns = lt + 1;
        int colon = x.indexOf(':', ns);
        if (colon > 0 && colon < gt) ns = colon + 1;
        if (strncasecmp(x.c_str() + ns, "response", 8) == 0 && (x[ns + 8] == '>' || x[ns + 8] == ' ')) { nextA = lt; break; }
      }
      j = gt + 1;
    }
    int b = nextA < 0 ? x.length() : nextA;
    pos = b;

    String href, len, mod, etag;
    if (!tagText(x, "href", a, b, href)) continue;
    tagText(x, "getcontentlength", a, b, len);
    tagText(x, "getlastmodified", a, b, mod);
    tagText(x, "getetag", a, b, etag);
    bool isDir = hasTag(x, "collection", a, b);
    href = decodePath(xmlUnescape(href));
    int scheme = href.indexOf("://");
    if (scheme >= 0) {
      int slash = href.indexOf('/', scheme + 3);
      href = slash < 0 ? "/" : href.substring(slash);
    }
    if (href.endsWith("/") && href.length() > 1) href.remove(href.length() - 1);
    if (!href.startsWith(baseHrefPath)) continue;
    String rel = href.substring(baseHrefPath.length());
    if (rel.isEmpty() || rel == selfRel) continue;  // the directory itself
    if (!rel.startsWith("/")) rel = "/" + rel;
    Remote r;
    r.dir = isDir;
    r.size = len.toInt();
    etag.trim();
    mod.trim();
    r.stamp = etag.length() ? etag : (mod + "|" + len);
    out[rel] = r;
  }
}

static const char *kPropfind =
    "<?xml version=\"1.0\"?><d:propfind xmlns:d=\"DAV:\"><d:prop><d:resourcetype/><d:getcontentlength/><d:getlastmodified/><d:getetag/></d:prop></d:propfind>";

static bool listRemoteDir(const String &rel, std::map<String, Remote> &out) {
  String body;
  int code = request("PROPFIND", rel, kPropfind, "1", &body);
  if (code == 404 && rel.length()) return true;
  if (code != 207) {
    lastError = "PROPFIND " + rel + " -> " + String(code);
    return false;
  }
  parseListing(body, rel, out);
  return true;
}

static bool scanRemote(const String &rel, std::map<String, Remote> &all, int depth = 0) {
  std::map<String, Remote> here;
  if (!listRemoteDir(rel, here)) return false;
  for (auto &kv : here) {
    all[kv.first] = kv.second;
    if (kv.second.dir && depth < 8) {
      if (!scanRemote(kv.first, all, depth + 1)) return false;
    }
  }
  return true;
}

static bool statRemote(const String &rel, Remote &r) {
  String body;
  int code = request("PROPFIND", rel, kPropfind, "0", &body);
  if (code != 207) return false;
  std::map<String, Remote> m;
  parseListing(body, "\x01", m);  // selfRel never matches: keep the entry itself
  if (m.empty()) return false;
  r = m.begin()->second;
  return true;
}

static std::set<String> madeDirs;
static bool ensureRemoteDir(const String &rel) {
  if (rel.isEmpty() || rel == "/" || madeDirs.count(rel)) return true;
  String parent = rel.substring(0, rel.lastIndexOf('/'));
  if (!ensureRemoteDir(parent)) return false;
  int code = request("MKCOL", rel, "", nullptr, nullptr);
  if (code == 201 || code == 405 || code == 200) {  // 405 = already exists
    madeDirs.insert(rel);
    return true;
  }
  lastError = "MKCOL " + rel + " -> " + String(code);
  return false;
}

static bool putFile(const String &rel, const String &localPath) {
  fs::File f = storage::fs().open(localPath, "r");
  if (!f) return false;
  if (!ensureRemoteDir(rel.substring(0, rel.lastIndexOf('/')))) { f.close(); return false; }
  Conn c;
  if (!c.begin(urlFor(rel))) { f.close(); return false; }
  int code = c.http.sendRequest("PUT", &f, f.size());
  c.http.end();
  f.close();
  if (code < 200 || code > 299) {
    lastError = "PUT " + rel + " -> " + String(code);
    return false;
  }
  return true;
}

static bool getFile(const String &rel, const String &localPath) {
  Conn c;
  if (!c.begin(urlFor(rel))) return false;
  int code = c.http.GET();
  if (code != 200) {
    c.http.end();
    lastError = "GET " + rel + " -> " + String(code);
    return false;
  }
  storage::mkdirs(storage::parent(localPath));
  String tmp = localPath + ".tmp";
  fs::File f = storage::fs().open(tmp, "w");
  if (!f) { c.http.end(); return false; }
  int n = c.http.writeToStream(&f);
  f.close();
  c.http.end();
  if (n < 0) { storage::fs().remove(tmp); lastError = "GET " + rel + " interrupted"; return false; }
  if (storage::fs().exists(localPath)) storage::fs().remove(localPath);
  return storage::fs().rename(tmp, localPath);
}

static bool delRemote(const String &rel) {
  int code = request("DELETE", rel, "", nullptr, nullptr);
  return code >= 200 && code < 300 || code == 404;
}

// ---- local tree ---------------------------------------------------------------------
static void scanLocal(const String &dir, const String &rootAbs, std::map<String, bool> &files) {
  std::vector<storage::Entry> items;
  if (!storage::list(dir, items)) return;
  for (auto &e : items) {
    String full = storage::join(dir, e.name);
    String rel = full.substring(rootAbs == "/" ? 0 : rootAbs.length());
    if (rel.isEmpty()) continue;
    if (e.dir) scanLocal(full, rootAbs, files);
    else files[rel] = true;
  }
}

// ---- state -------------------------------------------------------------------------------
// The state is only valid for the server URL + card folder it was recorded against.
// A file starts with "#target<TAB><key>"; any other key (or none: older firmware) means
// start fresh, so switching servers never reads as "deleted on the server".
static void loadState(std::map<String, State> &st, const String &target) {
  std::string d;
  if (!storage::readFile(kState, d)) return;
  std::string head = std::string("#target\t") + target.c_str() + "\n";
  if (d.compare(0, head.size(), head) != 0) return;
  size_t i = head.size();
  while (i < d.size()) {
    size_t e = d.find('\n', i);
    if (e == std::string::npos) e = d.size();
    std::string line = d.substr(i, e - i);
    size_t t1 = line.find('\t');
    size_t t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
    if (t2 != std::string::npos) {
      State s;
      s.crc = strtoul(line.substr(t1 + 1, t2 - t1 - 1).c_str(), nullptr, 16);
      s.stamp = line.substr(t2 + 1).c_str();
      st[String(line.substr(0, t1).c_str())] = s;
    }
    i = e + 1;
  }
}
static void saveState(const std::map<String, State> &st, const String &target) {
  std::string out = std::string("#target\t") + target.c_str() + "\n";
  char b[12];
  for (auto &kv : st) {
    snprintf(b, sizeof(b), "%08x", (unsigned)kv.second.crc);
    out += kv.first.c_str();
    out += '\t';
    out += b;
    out += '\t';
    out += kv.second.stamp.c_str();
    out += '\n';
  }
  storage::writeFile(kState, out.data(), out.size());
}

static void note(const String &s) {
  if (say) say(s);
}

Result run(void (*progress)(const String &line)) {
  Result res;
  say = progress;
  lastError = "";
  madeDirs.clear();
  if (!configured()) { res.error = "No server set up"; return res; }

  baseUrl = S.davUrl;
  while (baseUrl.endsWith("/")) baseUrl.remove(baseUrl.length() - 1);
  int s = baseUrl.indexOf("://");
  int slash = s < 0 ? baseUrl.indexOf('/') : baseUrl.indexOf('/', s + 3);
  baseHrefPath = slash < 0 ? "" : decodePath(baseUrl.substring(slash));

  String rootAbs = S.syncRoot[0] ? String(S.syncRoot) : String("/");
  if (!rootAbs.startsWith("/")) rootAbs = "/" + rootAbs;
  while (rootAbs.length() > 1 && rootAbs.endsWith("/")) rootAbs.remove(rootAbs.length() - 1);
  String localBase = rootAbs == "/" ? "" : rootAbs;  // rel paths start with '/'

  note("Reading server...");
  std::map<String, Remote> remote;
  if (!scanRemote("", remote)) { res.error = lastError.length() ? lastError : "Server unreachable"; return res; }
  note("Reading card...");
  std::map<String, bool> localFiles;
  scanLocal(rootAbs, rootAbs, localFiles);
  String target = baseUrl + "|" + rootAbs;
  std::map<String, State> state;
  loadState(state, target);

  std::set<String> all;
  for (auto &kv : remote) if (!kv.second.dir) all.insert(kv.first);
  for (auto &kv : localFiles) all.insert(kv.first);
  for (auto &kv : state) all.insert(kv.first);

  for (const String &rel : all) {
    // never sync our own bookkeeping
    if (rel.startsWith("/.")) continue;
    String lp = localBase + rel;
    bool L = localFiles.count(rel) > 0;
    auto rit = remote.find(rel);
    bool R = rit != remote.end() && !rit->second.dir;
    auto sit = state.find(rel);
    bool St = sit != state.end();
    uint32_t lcrc = L ? storage::crc32(lp) : 0;
    String name = storage::baseName(rel);

    auto recordFromNow = [&]() {
      Remote r;
      State ns;
      ns.crc = storage::crc32(lp);
      if (statRemote(rel, r)) ns.stamp = r.stamp;
      state[rel] = ns;
    };

    if (L && R && St) {
      bool lc = lcrc != sit->second.crc, rc = rit->second.stamp != sit->second.stamp;
      if (!lc && !rc) continue;
      if (lc && !rc) {
        note("Up: " + name);
        if (!putFile(rel, lp)) { res.error = lastError; break; }
        res.up++;
        recordFromNow();
      } else if (!lc && rc) {
        note("Down: " + name);
        if (!getFile(rel, lp)) { res.error = lastError; break; }
        res.down++;
        State ns;
        ns.crc = storage::crc32(lp);
        ns.stamp = rit->second.stamp;
        state[rel] = ns;
      } else {
        note("Conflict: " + name);
        String cp = storage::uniquePath(lp.substring(0, lp.lastIndexOf('.') > lp.lastIndexOf('/') ? lp.lastIndexOf('.') : lp.length()) + ".conflict" + (lp.lastIndexOf('.') > lp.lastIndexOf('/') ? lp.substring(lp.lastIndexOf('.')) : String("")));
        if (!getFile(rel, cp)) { res.error = lastError; break; }
        if (!putFile(rel, lp)) { res.error = lastError; break; }
        res.conflicts++;
        recordFromNow();
      }
    } else if (L && R && !St) {
      // seen on both sides but never synced: compare contents
      String tmp = lp + ".sync";
      if (!getFile(rel, tmp)) { res.error = lastError; break; }
      uint32_t rcrc = storage::crc32(tmp);
      if (rcrc == lcrc) {
        storage::fs().remove(tmp);
        State ns;
        ns.crc = lcrc;
        ns.stamp = rit->second.stamp;
        state[rel] = ns;
      } else {
        note("Conflict: " + name);
        String dot = lp.lastIndexOf('.') > lp.lastIndexOf('/') ? lp.substring(lp.lastIndexOf('.')) : String("");
        String stem = dot.length() ? lp.substring(0, lp.length() - dot.length()) : lp;
        String cp = storage::uniquePath(stem + ".conflict" + dot);
        storage::fs().rename(tmp, cp);
        if (!putFile(rel, lp)) { res.error = lastError; break; }
        res.conflicts++;
        recordFromNow();
      }
    } else if (L && !R && St) {
      if (lcrc == sit->second.crc) {  // deleted on the server
        note("Remove: " + name);
        storage::fs().remove(lp);
        state.erase(rel);
        res.removed++;
      } else {
        note("Up: " + name);
        if (!putFile(rel, lp)) { res.error = lastError; break; }
        res.up++;
        recordFromNow();
      }
    } else if (L && !R && !St) {
      note("Up: " + name);
      if (!putFile(rel, lp)) { res.error = lastError; break; }
      res.up++;
      recordFromNow();
    } else if (!L && R && St) {
      if (rit->second.stamp == sit->second.stamp) {  // deleted on the card
        note("Delete on server: " + name);
        if (!delRemote(rel)) { res.error = "DELETE " + rel; break; }
        state.erase(rel);
        res.removed++;
      } else {
        note("Down: " + name);
        if (!getFile(rel, lp)) { res.error = lastError; break; }
        res.down++;
        State ns;
        ns.crc = storage::crc32(lp);
        ns.stamp = rit->second.stamp;
        state[rel] = ns;
      }
    } else if (!L && R && !St) {
      note("Down: " + name);
      if (!getFile(rel, lp)) { res.error = lastError; break; }
      res.down++;
      State ns;
      ns.crc = storage::crc32(lp);
      ns.stamp = rit->second.stamp;
      state[rel] = ns;
    } else if (!L && !R && St) {
      state.erase(rel);
    }
  }
  saveState(state, target);
  res.ok = res.error.isEmpty();
  say = nullptr;
  return res;
}

}  // namespace davsync
