#include "rtc.h"
#include <Wire.h>
#include <sys/time.h>
#include "config.h"
#include "settings.h"

namespace rtc {

static bool ok = false;

static uint8_t bcd2(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t toBcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

void applyTz() {
  setenv("TZ", S.tz, 1);
  tzset();
}

static bool readHw(struct tm &t) {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x04);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)RTC_ADDR, 7) != 7) return false;
  uint8_t sec = Wire.read(), min = Wire.read(), hr = Wire.read(), day = Wire.read(), wd = Wire.read(), mon = Wire.read(), yr = Wire.read();
  if (sec & 0x80) return false;  // oscillator stopped: time not trustworthy
  memset(&t, 0, sizeof(t));
  t.tm_sec = bcd2(sec & 0x7F);
  t.tm_min = bcd2(min & 0x7F);
  t.tm_hour = bcd2(hr & 0x3F);
  t.tm_mday = bcd2(day & 0x3F);
  t.tm_wday = wd & 7;
  t.tm_mon = bcd2(mon & 0x1F) - 1;
  t.tm_year = bcd2(yr) + 100;
  t.tm_isdst = -1;
  return t.tm_year >= 124;
}

void begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  applyTz();
  struct tm t;
  if (readHw(t)) {
    ok = true;
    time_t now = mktime(&t);
    struct timeval tv = {now, 0};
    settimeofday(&tv, nullptr);
  }
}

bool valid() { return ok; }

bool localTime(struct tm &t) {
  if (!ok) return false;
  time_t now = time(nullptr);
  localtime_r(&now, &t);
  return true;
}

void setLocal(const struct tm &in) {
  struct tm t = in;
  t.tm_isdst = -1;
  time_t now = mktime(&t);
  struct timeval tv = {now, 0};
  settimeofday(&tv, nullptr);
  localtime_r(&now, &t);
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x04);
  Wire.write(toBcd(t.tm_sec));
  Wire.write(toBcd(t.tm_min));
  Wire.write(toBcd(t.tm_hour));
  Wire.write(toBcd(t.tm_mday));
  Wire.write(t.tm_wday);
  Wire.write(toBcd(t.tm_mon + 1));
  Wire.write(toBcd(t.tm_year - 100));
  Wire.endTransmission();
  ok = true;
}

void syncFromNtp(uint32_t timeoutMs) {
  configTzTime(S.tz, S.ntp);
  struct tm t;
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (getLocalTime(&t, 200) && t.tm_year >= 124) {
      setLocal(t);
      return;
    }
  }
}

String dateStamp() {
  struct tm t;
  if (!localTime(t)) return "undated";
  char b[16];
  strftime(b, sizeof(b), "%Y-%m-%d", &t);
  return b;
}

String longDate() {
  struct tm t;
  if (!localTime(t)) return "Journal";
  char b[48];
  strftime(b, sizeof(b), "%A, %B %d, %Y", &t);
  return b;
}

}  // namespace rtc
