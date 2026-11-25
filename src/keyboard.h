#pragma once
#include <Arduino.h>
#include <vector>
#include "keys.h"

// Input: BLE HID keyboard (pair once, auto-reconnects), USB serial for
// debugging, and the two board buttons (only as menu navigation when no
// keyboard is connected).
namespace kb {

enum State : uint8_t { ST_OFF, ST_IDLE, ST_SCANNING, ST_CONNECTING, ST_PAIRING, ST_CONNECTED, ST_FAILED };

struct Found {
  String name;
  String addr;
  uint8_t type;
  int rssi;
  bool hid;
};

void begin();
void poll();            // call every loop: decodes reports, key repeat, buttons
bool pop(Key &k);       // next input event
bool connected();
State state();
const char *stateText();
uint32_t passkey();     // non-zero while the keyboard wants this typed on it

void startScan();       // ~6 s, results via found()
bool scanning();
std::vector<Found> found();
void connectTo(const String &addr, uint8_t type);  // pair, remember on success
void reconnectSaved();  // most recently used saved keyboard (S.kbs[0])
int savedCount();
bool isConnectedTo(const char *addr);
void forget(const String &addr);  // drop one saved keyboard and its bond
void forgetAll();
void stop();            // shut the radio down (before sleep)

bool keyHeld();         // a hardware button was long-pressed: wants sleep
bool btMenuRequested(); // BOOT+KEY held ~1.5 s; true once per hold
bool sleepRequested();
void clearSleepRequest();

}  // namespace kb
