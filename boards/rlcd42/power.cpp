// RLCD board: battery on an ADC pin through a 3:1 divider; no PMIC.
#include "config.h"
#include "power.h"

namespace power {

static int batMv = 0;

void begin() {}

// Averages several ADC reads and smooths over time.
int percent() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(PIN_BAT);
  int mv = (int)(sum / 16) * 3;
  batMv = batMv ? (batMv * 3 + mv) / 4 : mv;
  return percentFromMv(batMv);
}

int millivolts() { return batMv; }
bool charging() { return false; }
void displayRail(bool) {}

}  // namespace power
