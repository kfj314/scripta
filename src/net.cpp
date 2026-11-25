#include "net.h"
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <algorithm>
#include "config.h"
#include "settings.h"
#include "storage.h"
#include "meta.h"

namespace net {

static bool apOn = false;
static DNSServer dns;
static const char *kApName = "Scripta-Setup";
static const char *kApPass = "scripta123";

bool online() { return WiFi.status() == WL_CONNECTED; }
String ip() { return online() ? WiFi.localIP().toString() : (apOn ? WiFi.softAPIP().toString() : String("")); }
String ssid() { return online() ? WiFi.SSID() : String(""); }

bool connect(const String &s, const String &pass, uint32_t timeoutMs) {
  if (apOn) stopAP();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(s.c_str(), pass.c_str());
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(100);
  }
  return false;
}

bool connectSaved(uint32_t timeoutMs) {
  if (online()) return true;
  if (!S.wifiSsid[0]) return false;
  return connect(S.wifiSsid, S.wifiPass, timeoutMs);
}

std::vector<Network> scan() {
  if (!apOn && WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks(false, false);
  std::vector<Network> out;
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.isEmpty()) continue;
    bool dup = false;
    for (auto &o : out) if (o.ssid == s) dup = true;
    if (dup) continue;
    out.push_back({s, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
  }
  WiFi.scanDelete();
  std::sort(out.begin(), out.end(), [](const Network &a, const Network &b) { return a.rssi > b.rssi; });
  return out;
}

void off() {
  web::stop();
  if (apOn) stopAP();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

bool startAP() {
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(kApName, kApPass)) return false;
  delay(200);
  dns.start(53, "*", WiFi.softAPIP());
  apOn = true;
  return true;
}

void stopAP() {
  if (!apOn) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  apOn = false;
  WiFi.mode(WIFI_STA);
}

bool apActive() { return apOn; }
String apName() { return kApName; }
String apPass() { return kApPass; }
void dnsTick() { if (apOn) dns.processNextRequest(); }

}  // namespace net

// ---------------------------------------------------------------------------
namespace web {

static WebServer *srv = nullptr;
static fs::File upFile;
static String upPath;
static bool upOk = false;

static String jsonEsc(const String &s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}
static String htmlEsc(const String &s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

static String cleanPath(String p) {
  p.replace("..", "");
  if (!p.startsWith("/")) p = "/" + p;
  while (p.indexOf("//") >= 0) p.replace("//", "/");
  if (p.length() > 1 && p.endsWith("/")) p.remove(p.length() - 1);
  return p;
}

static const char kIndex[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Scripta files</title><style>
body{font:15px system-ui,sans-serif;max-width:720px;margin:1em auto;padding:0 12px}
a{color:#06c;text-decoration:none}li{list-style:none;padding:6px 0;border-bottom:1px solid #ddd;display:flex;gap:8px}
li span.n{flex:1;word-break:break-all}button{font:inherit}.s{color:#777;font-size:12px}
</style></head><body>
<h2>Scripta <a href="/setup" style="font-size:13px">setup</a></h2>
<div id="path"></div>
<p><input type="file" id="f" multiple> <button onclick="up()">Upload</button>
<button onclick="mk()">New folder</button></p><ul id="l"></ul>
<script>
let cur='/';
const $=id=>document.getElementById(id);
async function ls(d){cur=d;$('path').innerHTML='<b>'+d+'</b>';
 const r=await (await fetch('/api/ls?d='+encodeURIComponent(d))).json();let h='';
 if(d!='/')h+='<li><a href="#" onclick="ls(\''+d.replace(/\/[^\/]*$/,'')+'\'||\'/\');return false">..</a></li>';
 for(const e of r){const p=(d=='/'?'':d)+'/'+e.n;
  h+='<li><span class="n">'+(e.d?'<a href="#" onclick="ls(decodeURIComponent(\''+encodeURIComponent(p)+'\'));return false">'+e.n+'/</a>':'<a href="/api/get?p='+encodeURIComponent(p)+'">'+e.n+'</a>')+'</span><span class="s">'+(e.d?'':e.s+' B')+'</span>'
  +'<button onclick="mv(decodeURIComponent(\''+encodeURIComponent(p)+'\'))">rename</button><button onclick="rm(decodeURIComponent(\''+encodeURIComponent(p)+'\'))">delete</button></li>';}
 $('l').innerHTML=h||'<li>(empty)</li>';}
async function up(){for(const f of $('f').files){const fd=new FormData();fd.append('f',f,f.name);
 await fetch('/api/upload?d='+encodeURIComponent(cur),{method:'POST',body:fd});}ls(cur);$('f').value='';}
async function mk(){const n=prompt('Folder name');if(!n)return;await fetch('/api/mkdir?p='+encodeURIComponent((cur=='/'?'':cur)+'/'+n),{method:'POST'});ls(cur);}
async function rm(p){if(!confirm('Delete '+p+'?'))return;await fetch('/api/rm?p='+encodeURIComponent(p),{method:'POST'});ls(cur);}
async function mv(p){const n=prompt('Move/rename to',p);if(!n||n==p)return;await fetch('/api/mv?a='+encodeURIComponent(p)+'&b='+encodeURIComponent(n),{method:'POST'});ls(cur);}
ls('/');
</script></body></html>)HTML";

static void sendIndex() { srv->send_P(200, "text/html", kIndex); }

static void apiLs() {
  String d = cleanPath(srv->arg("d"));
  std::vector<storage::Entry> items;
  if (!storage::list(d, items)) { srv->send(404, "application/json", "[]"); return; }
  String o = "[";
  for (size_t i = 0; i < items.size(); i++) {
    if (i) o += ",";
    o += "{\"n\":\"" + jsonEsc(items[i].name) + "\",\"d\":" + (items[i].dir ? "true" : "false") + ",\"s\":" + String(items[i].size) + "}";
  }
  o += "]";
  srv->send(200, "application/json", o);
}

static void apiGet() {
  String p = cleanPath(srv->arg("p"));
  fs::File f = storage::fs().open(p, "r");
  if (!f || f.isDirectory()) { srv->send(404, "text/plain", "not found"); return; }
  srv->sendHeader("Content-Disposition", "attachment; filename=\"" + storage::baseName(p) + "\"");
  srv->streamFile(f, "application/octet-stream");
  f.close();
}

static void apiDone() { srv->send(upOk ? 200 : 500, "text/plain", upOk ? "ok" : "failed"); }

static void apiUpload() {
  HTTPUpload &u = srv->upload();
  if (u.status == UPLOAD_FILE_START) {
    String d = cleanPath(srv->arg("d"));
    String name = u.filename;
    name.replace("/", "_");
    upPath = storage::join(d, name);
    storage::mkdirs(d);
    upOk = false;
    upFile = storage::fs().open(upPath, "w");
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (upFile) upFile.write(u.buf, u.currentSize);
  } else if (u.status == UPLOAD_FILE_END) {
    if (upFile) { upFile.close(); upOk = true; }
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (upFile) upFile.close();
    storage::fs().remove(upPath);
  }
}

static void apiMkdir() {
  String p = cleanPath(srv->arg("p"));
  srv->send(storage::mkdirs(p) ? 200 : 500, "text/plain", "ok");
}
static void apiRm() {
  String p = cleanPath(srv->arg("p"));
  if (p == "/" || p.startsWith(META_DIR)) { srv->send(403, "text/plain", "no"); return; }
  bool ok = storage::removeAll(p);
  meta::removed(p);
  srv->send(ok ? 200 : 500, "text/plain", ok ? "ok" : "failed");
}
static void apiMv() {
  String a = cleanPath(srv->arg("a")), b = cleanPath(srv->arg("b"));
  storage::mkdirs(storage::parent(b));
  bool ok = storage::rename(a, b);
  if (ok) meta::moved(a, b);
  srv->send(ok ? 200 : 500, "text/plain", ok ? "ok" : "failed");
}

static String field(const char *label, const char *name, const String &val, const char *type = "text", const char *extra = "") {
  return String("<p><label>") + label + "<br><input name=\"" + name + "\" type=\"" + type + "\" value=\"" + htmlEsc(val) + "\" " + extra + " style=\"width:100%\"></label></p>";
}

static void setupPage() {
  String h = "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Scripta setup</title>"
             "<style>body{font:16px system-ui,sans-serif;max-width:560px;margin:1em auto;padding:0 12px}input,button{font:inherit;padding:6px}</style></head><body>"
             "<h2>Scripta setup</h2><form method=\"post\" action=\"/setup\">";
  h += "<h3>Wi-Fi</h3>";
  h += field("Network name (SSID)", "ssid", S.wifiSsid, "text", "list=nets");
  h += "<datalist id=\"nets\">";
  for (auto &n : net::scan()) h += "<option value=\"" + htmlEsc(n.ssid) + "\">";
  h += "</datalist>";
  h += field("Password (blank = keep)", "pass", "", "password");
  h += "<h3>Sync (WebDAV)</h3>";
  h += field("Server URL, e.g. https://nas.local:8080/scripta", "dav", S.davUrl);
  h += field("User", "du", S.davUser);
  h += field("Password (blank = keep)", "dp", "", "password");
  h += field("Folder on the card to sync", "root", S.syncRoot);
  h += "<h3>Clock</h3>";
  h += field("Time zone (POSIX), e.g. EST5EDT,M3.2.0,M11.1.0", "tz", S.tz);
  h += field("NTP server", "ntp", S.ntp);
  h += "<p><button>Save</button> <a href=\"/\">Files</a></p></form></body></html>";
  srv->send(200, "text/html", h);
}

static void setupSave() {
  auto cp = [](char *dst, size_t n, const String &v, bool keepIfEmpty) {
    if (keepIfEmpty && v.isEmpty()) return;
    strlcpy(dst, v.c_str(), n);
  };
  cp(S.wifiSsid, sizeof(S.wifiSsid), srv->arg("ssid"), false);
  cp(S.wifiPass, sizeof(S.wifiPass), srv->arg("pass"), true);
  cp(S.davUrl, sizeof(S.davUrl), srv->arg("dav"), false);
  cp(S.davUser, sizeof(S.davUser), srv->arg("du"), false);
  cp(S.davPass, sizeof(S.davPass), srv->arg("dp"), true);
  cp(S.syncRoot, sizeof(S.syncRoot), srv->arg("root").isEmpty() ? String("/") : srv->arg("root"), false);
  cp(S.tz, sizeof(S.tz), srv->arg("tz"), false);
  cp(S.ntp, sizeof(S.ntp), srv->arg("ntp"), false);
  settingsSave();
  srv->send(200, "text/html", "<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\"><body style=\"font:16px system-ui;margin:2em\"><h3>Saved</h3><p>You can close this page. On the device, choose Wi-Fi, then Connect.</p>");
}

void start() {
  if (srv) return;
  srv = new WebServer(80);
  srv->on("/", HTTP_GET, []() {
    if (net::apActive() && !net::online()) { srv->sendHeader("Location", "/setup"); srv->send(302, "text/plain", ""); return; }
    sendIndex();
  });
  srv->on("/setup", HTTP_GET, setupPage);
  srv->on("/setup", HTTP_POST, setupSave);
  srv->on("/api/ls", HTTP_GET, apiLs);
  srv->on("/api/get", HTTP_GET, apiGet);
  srv->on("/api/upload", HTTP_POST, apiDone, apiUpload);
  srv->on("/api/mkdir", HTTP_POST, apiMkdir);
  srv->on("/api/rm", HTTP_POST, apiRm);
  srv->on("/api/mv", HTTP_POST, apiMv);
  srv->onNotFound([]() {
    srv->sendHeader("Location", String("http://") + (net::apActive() ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "/setup");
    srv->send(302, "text/plain", "");
  });
  srv->begin();
}

void stop() {
  if (!srv) return;
  srv->stop();
  delete srv;
  srv = nullptr;
}

bool running() { return srv != nullptr; }

void loop() {
  if (srv) srv->handleClient();
  net::dnsTick();
}

}  // namespace web
