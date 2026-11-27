#pragma once
#include <Arduino.h>

// Battery and power rails. Each board implements this in
// boards/<name>/power.cpp (an ADC divider on the RLCD board, the TG28 /
// AXP2101 PMIC on the e-paper board).
namespace power {
void begin();            // call first: on the e-paper board it powers the panel
int percent();           // 0-100, smoothed
int millivolts();        // last battery reading
bool charging();
void displayRail(bool on);  // e-paper supply (no-op on the RLCD board)

// Shared helper: single-cell Li-ion resting voltage (mV) -> percent.
int percentFromMv(int mv);
}  // namespace power
