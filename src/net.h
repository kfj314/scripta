#pragma once
#include <Arduino.h>
#include <vector>

// Wi-Fi station / hotspot helpers and the small HTTP server (file manager and
// phone setup page).
namespace net {

struct Network {
  String ssid;
  int rssi;
  bool secure;
};

bool online();
String ip();
String ssid();
bool connectSaved(uint32_t timeoutMs);
bool connect(const String &ssid, const String &pass, uint32_t timeoutMs);
std::vector<Network> scan();
void off();

bool startAP();  // open "Scripta-Setup" hotspot with captive portal
void stopAP();
bool apActive();
String apName();
String apPass();
void dnsTick();

}  // namespace net

namespace web {
void start();
void stop();
bool running();
void loop();
}  // namespace web
