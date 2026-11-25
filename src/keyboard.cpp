#include "keyboard.h"
#include <NimBLEDevice.h>
#include "config.h"
#include "settings.h"

namespace kb {

enum Cmd : uint8_t { C_SCAN, C_CONNECT, C_SAVED, C_STOP };
struct CmdMsg {
  Cmd cmd;
  char addr[18];
  uint8_t type;
};

struct Report {
  uint8_t d[8];
};

static QueueHandle_t cmdQ = nullptr;
static QueueHandle_t reportQ = nullptr;
static QueueHandle_t keyQ = nullptr;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static volatile State st = ST_OFF;
static volatile uint32_t pk = 0;
static std::vector<Found> results;
static NimBLEClient *client = nullptr;
static bool saveOnConnect = false;
static char pendingAddr[18];
static uint8_t pendingType = 0;
static bool wantReconnect = false;
static uint32_t nextReconnect = 0;
static bool inited = false;
static volatile bool sleepReq = false;
static String failWhy;
static int tryIdx = 0;              // which saved keyboard the reconnect loop tries next
static volatile bool btReq = false;  // BOOT+KEY combo: open the Bluetooth menu
#define BLOG(...) Serial.printf("[bt] " __VA_ARGS__)
static void fail(const char *why) {
  failWhy = why;
  BLOG("FAIL: %s\n", why);
  st = ST_FAILED;
}

// ---- HID decoding ---------------------------------------------------------
static const char kUnshift[] = "abcdefghijklmnopqrstuvwxyz1234567890";
static uint32_t mapUsage(uint8_t u, bool shift, bool forceBase) {
  if (u >= 0x04 && u <= 0x1D) {
    char c = 'a' + (u - 0x04);
    return (shift && !forceBase) ? (c - 32) : c;
  }
  if (u >= 0x1E && u <= 0x27) {
    static const char sh[] = "!@#$%^&*()";
    int i = u - 0x1E;
    return (shift && !forceBase) ? sh[i] : kUnshift[26 + i];
  }
  struct P { uint8_t u; char n, s; };
  static const P tbl[] = {{0x2C, ' ', ' '}, {0x2D, '-', '_'}, {0x2E, '=', '+'}, {0x2F, '[', '{'}, {0x30, ']', '}'},
                          {0x31, '\\', '|'}, {0x33, ';', ':'}, {0x34, '\'', '"'}, {0x35, '`', '~'}, {0x36, ',', '<'},
                          {0x37, '.', '>'}, {0x38, '/', '?'}};
  for (auto &p : tbl)
    if (p.u == u) return (shift && !forceBase) ? p.s : p.n;
  switch (u) {
    case 0x28: return K_ENTER;
    case 0x29: return K_ESC;
    case 0x2A: return K_BACKSPACE;
    case 0x2B: return K_TAB;
    case 0x4C: return K_DELETE;
    case 0x4F: return K_RIGHT;
    case 0x50: return K_LEFT;
    case 0x51: return K_DOWN;
    case 0x52: return K_UP;
    case 0x4A: return K_HOME;
    case 0x4D: return K_END;
    case 0x4B: return K_PGUP;
    case 0x4E: return K_PGDN;
    case 0x58: return K_ENTER;  // keypad enter
    default: break;
  }
  if (u >= 0x3A && u <= 0x45) return K_F1 + (u - 0x3A);
  return K_NONE;
}

static bool capsOn = false;
static uint8_t prevKeys[6] = {0};
static uint8_t lastMods = 0;
static uint8_t heldUsage = 0;
static uint32_t nextRepeat = 0;

static void emit(uint8_t usage, uint8_t modByte) {
  if (usage == 0x39) { capsOn = !capsOn; return; }
  bool ctrl = modByte & 0x11 || modByte & 0x88;  // ctrl or gui (cmd)
  bool shift = modByte & 0x22;
  bool alt = modByte & 0x44;
  bool base = ctrl || alt;
  uint32_t c = mapUsage(usage, shift, base);
  if (c == K_NONE) return;
  if (capsOn && c >= 'a' && c <= 'z' && !base) c -= 32;
  else if (capsOn && c >= 'A' && c <= 'Z' && !base) c += 32;
  Key k;
  k.code = c;
  k.mods = (ctrl ? M_CTRL : 0) | (shift ? M_SHIFT : 0) | (alt ? M_ALT : 0);
  xQueueSend(keyQ, &k, 0);
}

static void handleReport(const Report &r) {
  uint8_t mods = r.d[0];
  const uint8_t *keys = r.d + 2;
  for (int i = 0; i < 6; i++) {
    uint8_t u = keys[i];
    if (u < 4) continue;
    bool was = false;
    for (int j = 0; j < 6; j++) if (prevKeys[j] == u) was = true;
    if (!was) {
      emit(u, mods);
      heldUsage = u;
      nextRepeat = millis() + 450;
    }
  }
  bool stillHeld = false;
  for (int i = 0; i < 6; i++) if (keys[i] == heldUsage && heldUsage) stillHeld = true;
  if (!stillHeld) heldUsage = 0;
  memcpy(prevKeys, keys, 6);
  lastMods = mods;
}

static void notifyCb(NimBLERemoteCharacteristic *c, uint8_t *data, size_t len, bool) {
  Serial.printf("[bt] report from %s len %u:", c->getUUID().toString().c_str(), (unsigned)len);
  for (size_t i = 0; i < len && i < 16; i++) Serial.printf(" %02x", data[i]);
  Serial.println();
  Report r;
  if (len == 8) memcpy(r.d, data, 8);
  else if (len == 9 && data[0] == 1) memcpy(r.d, data + 1, 8);
  else return;
  xQueueSend(reportQ, &r, 0);
}

// ---- saved keyboards --------------------------------------------------------
static int savedN() {
  int n = 0;
  while (n < KB_SLOTS && S.kbs[n].addr[0]) n++;
  return n;
}
static int findSaved(const char *addr) {
  for (int i = 0; i < savedN(); i++)
    if (strcasecmp(S.kbs[i].addr, addr) == 0) return i;
  return -1;
}
static String scannedName(const char *addr) {
  String n;
  portENTER_CRITICAL(&mux);
  for (auto &f : results)
    if (f.addr.equalsIgnoreCase(addr) && f.name != f.addr) n = f.name;
  portEXIT_CRITICAL(&mux);
  return n;
}
// Move (or add) a keyboard to the front of the list. Saves only if something changed.
static void remember(const char *addr, uint8_t type) {
  int i = findSaved(addr);
  String n = scannedName(addr);
  if (i == 0 && S.kbs[0].type == type && (n.isEmpty() || n == S.kbs[0].name)) return;
  KbSaved k = {};
  if (i >= 0) k = S.kbs[i];
  strlcpy(k.addr, addr, sizeof(k.addr));
  k.type = type;
  if (n.length()) strlcpy(k.name, n.c_str(), sizeof(k.name));
  int last = i >= 0 ? i : KB_SLOTS - 1;  // slot that the shift overwrites
  if (i < 0 && S.kbs[last].addr[0]) {
    BLOG("list full, dropping %s\n", S.kbs[last].addr);
    NimBLEDevice::deleteBond(NimBLEAddress(std::string(S.kbs[last].addr), S.kbs[last].type));
  }
  for (int j = last; j > 0; j--) S.kbs[j] = S.kbs[j - 1];
  S.kbs[0] = k;
  settingsSave();
}

// ---- BLE worker -------------------------------------------------------------
class ClientCb : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *) override {}
  void onDisconnect(NimBLEClient *, int reason) override {
    BLOG("disconnected reason %d\n", reason);
    if (st == ST_CONNECTED || st == ST_CONNECTING || st == ST_PAIRING) st = ST_IDLE;
    pk = 0;
    memset(prevKeys, 0, 6);
    heldUsage = 0;
    if (savedN()) {
      wantReconnect = true;
      nextReconnect = millis() + 3000;
      tryIdx = 0;
    }
  }
  void onConnectFail(NimBLEClient *, int reason) override { BLOG("connect fail reason %d\n", reason); }
  uint32_t onPassKeyDisplay(NimBLEConnInfo &) override {
    BLOG("passkey display requested\n");
    uint32_t p = esp_random() % 1000000;
    pk = p ? p : 1;
    st = ST_PAIRING;
    return p;
  }
  void onConfirmPasskey(NimBLEConnInfo &info, uint32_t pin) override {
    pk = pin;
    st = ST_PAIRING;
    NimBLEDevice::injectConfirmPasskey(info, true);
  }
  void onAuthenticationComplete(NimBLEConnInfo &i) override {
    BLOG("auth complete encrypted=%d bonded=%d\n", i.isEncrypted(), i.isBonded());
    pk = 0;
  }
};
static ClientCb clientCb;

static void doScan() {
  st = ST_SCANNING;
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(80);
  NimBLEScanResults res = scan->getResults(6000, false);
  std::vector<Found> out;
  NimBLEUUID hid((uint16_t)0x1812);
  for (int i = 0; i < res.getCount(); i++) {
    const NimBLEAdvertisedDevice *d = res.getDevice(i);
    Found f;
    f.name = d->getName().c_str();
    f.addr = d->getAddress().toString().c_str();
    f.type = d->getAddressType();
    f.rssi = d->getRSSI();
    f.hid = d->isAdvertisingService(hid) || d->getAppearance() == 0x03C1;
    BLOG("found '%s' %s type %u rssi %d hid=%d appearance=0x%04x\n", f.name.c_str(), f.addr.c_str(), f.type, f.rssi, f.hid, d->getAppearance());
    if (f.name.isEmpty()) f.name = f.addr;
    out.push_back(f);
  }
  scan->clearResults();
  portENTER_CRITICAL(&mux);
  results = out;
  portEXIT_CRITICAL(&mux);
  st = (client && client->isConnected()) ? ST_CONNECTED : ST_IDLE;
  // Give the user time to pick from the list before auto-reconnect ties up the radio.
  nextReconnect = millis() + 20000;
}

static bool doConnect(const char *addr, uint8_t type, bool save) {
  if (client && client->isConnected()) {
    if (strcasecmp(client->getPeerAddress().toString().c_str(), addr) == 0) {
      BLOG("already connected to %s\n", addr);
      st = ST_CONNECTED;
      return true;
    }
    client->disconnect();
  }
  st = ST_CONNECTING;
  pk = 0;
  if (!client) {
    client = NimBLEDevice::createClient();
    client->setClientCallbacks(&clientCb, false);
    client->setConnectTimeout(10000);
  }
  BLOG("connecting to %s type %u\n", addr, type);
  if (!client->connect(NimBLEAddress(std::string(addr), type), true, false, false)) {
    char b[48];
    snprintf(b, sizeof(b), "link failed (err %d)", client->getLastError());
    fail(b);
    return false;
  }
  BLOG("link up, securing\n");
  // Pairing happens here; keyboards that need a passkey call onPassKeyDisplay.
  if (!client->secureConnection()) {
    BLOG("security not completed (continuing)\n");
  } else {
    BLOG("secured\n");
  }
  if (!client->isConnected()) { fail("dropped during pairing"); return false; }
  NimBLERemoteService *svc = client->getService("1812");
  if (!svc) {
    client->disconnect();
    fail("no HID service (not a keyboard?)");
    return false;
  }
  BLOG("HID service found\n");
  NimBLERemoteCharacteristic *boot = nullptr;
  NimBLERemoteCharacteristic *mode = nullptr;
  std::vector<NimBLERemoteCharacteristic *> reps;
  for (auto *c : svc->getCharacteristics(true)) {
    uint16_t u16 = 0;
    if (c->getUUID().bitSize() == 16) u16 = *(uint16_t *)(c->getUUID().getValue());
    BLOG("  char %s notify=%d read=%d write=%d\n", c->getUUID().toString().c_str(), c->canNotify(), c->canRead(), c->canWrite());
    if (u16 == 0x2A22) boot = c;
    else if (u16 == 0x2A4E) mode = c;
    else if (u16 == 0x2A4D && c->canNotify()) reps.push_back(c);
  }
  // Subscribe to every input channel: keyboards differ on which one they use.
  bool ok = false;
  if (boot && boot->canNotify()) {
    bool r = boot->subscribe(true, notifyCb);
    BLOG("subscribe boot: %d\n", r);
    ok |= r;
  }
  for (auto *c : reps) {
    bool r = c->subscribe(true, notifyCb);
    BLOG("subscribe report: %d\n", r);
    ok |= r;
  }
  (void)mode;
  BLOG("boot=%d reports=%d subscribed=%d\n", boot != nullptr, (int)reps.size(), ok);
  if (!ok) {
    client->disconnect();
    fail("could not subscribe to key reports");
    return false;
  }
  pk = 0;
  st = ST_CONNECTED;
  wantReconnect = false;
  tryIdx = 0;
  if (save || findSaved(addr) >= 0) remember(addr, type);
  return true;
}

static void worker(void *) {
  NimBLEDevice::init("Scripta");
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setPower(3);
  st = ST_IDLE;
  inited = true;
  if (savedN()) {
    wantReconnect = true;
    nextReconnect = millis();
  }
  CmdMsg m;
  while (true) {
    if (xQueueReceive(cmdQ, &m, 200 / portTICK_PERIOD_MS) == pdTRUE) {
      switch (m.cmd) {
        case C_SCAN: doScan(); break;
        case C_CONNECT: doConnect(m.addr, m.type, true); break;
        case C_SAVED:
          if (savedN()) doConnect(S.kbs[0].addr, S.kbs[0].type, false);
          break;
        case C_STOP:
          if (client && client->isConnected()) client->disconnect();
          wantReconnect = false;
          NimBLEDevice::deinit(true);
          st = ST_OFF;
          vTaskDelete(nullptr);
          return;
      }
    } else if (wantReconnect && savedN() && st == ST_IDLE && millis() >= nextReconnect) {
      // Try each saved keyboard in turn (most recent first); whichever is awake connects.
      int n = savedN();
      int i = tryIdx % n;
      KbSaved k = S.kbs[i];
      if (!doConnect(k.addr, k.type, false)) {
        st = ST_IDLE;
        tryIdx = (i + 1) % n;
        nextReconnect = millis() + (tryIdx == 0 ? 6000 : 500);
      }
    }
  }
}

void begin() {
  cmdQ = xQueueCreate(4, sizeof(CmdMsg));
  reportQ = xQueueCreate(16, sizeof(Report));
  keyQ = xQueueCreate(64, sizeof(Key));
  pinMode(PIN_BOOT, INPUT_PULLUP);
  pinMode(PIN_KEY, INPUT_PULLUP);
  xTaskCreatePinnedToCore(worker, "ble", 8192, nullptr, 2, nullptr, 0);
}

static void send(Cmd c, const String &a = "", uint8_t t = 0) {
  CmdMsg m = {};
  m.cmd = c;
  strlcpy(m.addr, a.c_str(), sizeof(m.addr));
  m.type = t;
  xQueueSend(cmdQ, &m, 0);
}

void startScan() { if (st != ST_SCANNING) send(C_SCAN); }
bool scanning() { return st == ST_SCANNING; }
std::vector<Found> found() {
  portENTER_CRITICAL(&mux);
  std::vector<Found> r = results;
  portEXIT_CRITICAL(&mux);
  return r;
}
void connectTo(const String &addr, uint8_t type) { send(C_CONNECT, addr, type); }
void reconnectSaved() { send(C_SAVED); }
int savedCount() { return savedN(); }
void forget(const String &addr) {
  int i = findSaved(addr.c_str());
  if (i < 0) return;
  KbSaved k = S.kbs[i];
  for (int j = i; j < KB_SLOTS - 1; j++) S.kbs[j] = S.kbs[j + 1];
  memset(&S.kbs[KB_SLOTS - 1], 0, sizeof(KbSaved));
  tryIdx = 0;
  if (!savedN()) wantReconnect = false;
  settingsSave();
  if (client && client->isConnected() && strcasecmp(client->getPeerAddress().toString().c_str(), k.addr) == 0)
    client->disconnect();
  NimBLEDevice::deleteBond(NimBLEAddress(std::string(k.addr), k.type));
}
void forgetAll() {
  memset(S.kbs, 0, sizeof(S.kbs));
  wantReconnect = false;
  settingsSave();
  if (client && client->isConnected()) client->disconnect();
  NimBLEDevice::deleteAllBonds();
}
void stop() { send(C_STOP); }

bool connected() { return st == ST_CONNECTED && client && client->isConnected(); }
bool isConnectedTo(const char *addr) {
  return connected() && strcasecmp(client->getPeerAddress().toString().c_str(), addr) == 0;
}
State state() { return st; }
uint32_t passkey() { return pk; }
const char *stateText() {
  switch (st) {
    case ST_OFF: return "Bluetooth off";
    case ST_IDLE: return savedN() ? "Keyboard not connected" : "No keyboard paired";
    case ST_SCANNING: return "Scanning...";
    case ST_CONNECTING: return "Connecting...";
    case ST_PAIRING: return "Pairing";
    case ST_CONNECTED: return "Keyboard connected";
    case ST_FAILED: return failWhy.length() ? failWhy.c_str() : "Connection failed";
  }
  return "";
}

// ---- buttons + serial ---------------------------------------------------------
static uint32_t bootDown = 0, keyDown = 0;
static bool bootLong = false, keyLong = false;

static void pushKey(uint32_t code, uint8_t mods = 0) {
  Key k;
  k.code = code;
  k.mods = mods;
  xQueueSend(keyQ, &k, 0);
}

static uint32_t bothDown = 0;
static bool bothFired = false;

static void pollButtons() {
  uint32_t now = millis();
  bool b = digitalRead(PIN_BOOT) == LOW, k = digitalRead(PIN_KEY) == LOW;
  if (b && !bootDown) { bootDown = now; bootLong = false; }
  if (k && !keyDown) { keyDown = now; keyLong = false; }
  // BOOT+KEY held together: open Bluetooth and scan. This swallows both buttons'
  // own tap/hold actions (no up/down/select, no sleep) until they are released.
  if (b && k) {
    if (!bothDown) { bothDown = now; bothFired = false; }
    bootLong = keyLong = true;
    if (!bothFired && now - bothDown > 1500) {
      btReq = true;
      bothFired = true;
    }
  } else {
    bothDown = 0;
  }
  if (b && bootDown && !bootLong && now - bootDown > 700) {
    bootLong = true;
    if (!connected()) pushKey(K_UP);
  }
  if (!b && bootDown) {
    if (!bootLong && !connected()) pushKey(K_DOWN);
    bootDown = 0;
  }
  if (k && keyDown && !keyLong && now - keyDown > 1200) {
    keyLong = true;
    sleepReq = true;
  }
  if (!k && keyDown) {
    if (!keyLong && !connected()) pushKey(K_ENTER);
    keyDown = 0;
  }
}

static void pollSerial() {
  while (Serial.available()) {
    int c = Serial.read();
    if (c == '\r') pushKey(K_ENTER);
    else if (c == '\n') continue;
    else if (c == 0x7F || c == 0x08) pushKey(K_BACKSPACE);
    else if (c == 0x09) pushKey(K_TAB);
    else if (c == 0x1B) pushKey(K_ESC);
    else if (c >= 1 && c <= 26) pushKey('a' + c - 1, M_CTRL);
    else if (c >= 32 && c < 127) pushKey(c);
  }
}

void poll() {
  Report r;
  while (xQueueReceive(reportQ, &r, 0) == pdTRUE) handleReport(r);
  if (heldUsage && millis() >= nextRepeat) {
    emit(heldUsage, lastMods);
    nextRepeat = millis() + 35;
  }
  pollButtons();
  pollSerial();
}

bool pop(Key &k) { return keyQ && xQueueReceive(keyQ, &k, 0) == pdTRUE; }
bool sleepRequested() { return sleepReq; }
void clearSleepRequest() { sleepReq = false; }
bool keyHeld() { return keyDown != 0; }
bool btMenuRequested() {
  bool r = btReq;
  btReq = false;
  return r;
}

}  // namespace kb
