#pragma once

// ============================================================
// Battery: reads the battery ADC (divider already on the board)
// and converts voltage to an approximate percentage (Li-ion
// discharge curve). Readings are cached and refreshed at most
// every few seconds - callers may poll every frame for free
// (the ADC is slow-ish I/O and battery state changes slowly).
// ============================================================

class Battery {
 public:
  void begin();
  float voltage() const;  // volts (cached)
  int percent() const;    // 0..100 (cached)

 private:
  void refresh() const;   // samples the ADC when the cache is stale

  mutable float _volts = 0.0f;
  mutable unsigned long _sampledAt = 0;  // 0 = never sampled
};
