// E-paper board: TG28 PMIC (AXP2101-compatible) over I2C. It supplies the
// e-paper panel from ALDO3 and has a fuel gauge.
#include <Wire.h>
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
#include "config.h"
#include "power.h"

namespace power {

static XPowersPMU pmu;
static bool pmuOk = false;
static int batMv = 0;

void begin() {
  pmuOk = pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL);
  if (!pmuOk) {
    Serial.println("[power] PMIC not found");
    return;
  }
  // Same rail and charger setup as Waveshare's demo firmware.
  pmu.setVbusVoltageLimit(XPOWERS_AXP2101_VBUS_VOL_LIM_4V36);
  pmu.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_1500MA);
  pmu.setALDO3Voltage(3300);
  pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
  pmu.setPowerKeyPressOnTime(XPOWERS_POWERON_1S);
  pmu.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
  pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_200MA);
  pmu.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_25MA);
  pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
  pmu.enableBattDetection();
  pmu.enableBattVoltageMeasure();
  pmu.enableVbusVoltageMeasure();
  pmu.enableSystemVoltageMeasure();
  displayRail(true);
}

int percent() {
  if (!pmuOk) return 100;
  int mv = pmu.getBattVoltage();
  if (mv > 0) batMv = batMv ? (batMv * 3 + mv) / 4 : mv;
  // Prefer the PMIC's fuel gauge; fall back to the voltage curve.
  int p = pmu.isBatteryConnect() ? pmu.getBatteryPercent() : -1;
  if (p >= 0 && p <= 100) return p;
  return batMv ? percentFromMv(batMv) : 100;
}

int millivolts() { return batMv; }
bool charging() { return pmuOk && pmu.isCharging(); }

void displayRail(bool on) {
  if (!pmuOk) return;
  if (on) pmu.enableALDO3();
  else pmu.disableALDO3();
}

}  // namespace power
