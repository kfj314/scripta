#include "power.h"

namespace power {

// Single-cell Li-ion resting-voltage curve (mV -> %), interpolated.
int percentFromMv(int mv) {
  static const int curve[][2] = {{3300, 0}, {3500, 5}, {3600, 12}, {3700, 25}, {3750, 38}, {3800, 50},
                                 {3870, 62}, {3950, 75}, {4050, 88}, {4150, 97}, {4200, 100}};
  const int n = sizeof(curve) / sizeof(curve[0]);
  if (mv <= curve[0][0]) return 0;
  for (int i = 1; i < n; i++) {
    if (mv <= curve[i][0]) {
      int span = curve[i][0] - curve[i - 1][0];
      return curve[i - 1][1] + (curve[i][1] - curve[i - 1][1]) * (mv - curve[i - 1][0]) / span;
    }
  }
  return 100;
}

}  // namespace power
