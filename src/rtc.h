#pragma once
#include <Arduino.h>
#include <time.h>

// PCF85063A real-time clock on the I2C bus; mirrors into the system clock.
namespace rtc {
void begin();
bool valid();                 // clock has been set at some point
bool localTime(struct tm &t); // system clock (local, per TZ) if valid
void setLocal(const struct tm &t);
void applyTz();
void syncFromNtp(uint32_t timeoutMs);  // needs wifi up
String dateStamp();           // 2026-10-21
String longDate();            // Wednesday, October 21, 2026
}  // namespace rtc
