#pragma once

// ============================================================
// Battery: reads the battery ADC (divider already on the board)
// and converts voltage to an approximate percentage (Li-ion
// discharge curve). The table in percent() is approximate;
// calibrate against the real resting voltage of your battery.
// ============================================================

class Battery {
 public:
  void begin();
  float voltage() const;  // volts
  int percent() const;    // 0..100
};
