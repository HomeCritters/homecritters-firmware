#pragma once
// ============================================================
// Game tuning parameters. All game balancing lives here, away
// from the logic. Tweak these to rebalance the pet.
// ============================================================

namespace game {

// --- Initial value for every stat (0..100) ---
constexpr float INITIAL_STAT = 80.0f;

// --- Decay rates (points per minute, while awake) ---
constexpr float HUNGER_DECAY_PER_MIN  = 1.2f;
constexpr float ENERGY_DECAY_PER_MIN  = 0.8f;
constexpr float JOY_DECAY_PER_MIN     = 1.0f;
constexpr float HYGIENE_DECAY_PER_MIN = 0.6f;

// While sleeping, overall decay is scaled down by this factor...
constexpr float SLEEP_DECAY_FACTOR    = 0.4f;
// ...and energy RECOVERS at this rate (points per minute).
constexpr float ENERGY_RECOVER_PER_MIN = 4.0f;

// --- Gains per action ---
constexpr float FEED_GAIN  = 25.0f;
constexpr float PAT_GAIN   = 15.0f;
constexpr float CLEAN_GAIN = 30.0f;

// --- Mood thresholds ---
constexpr float HUNGRY_THRESHOLD  = 25.0f;
constexpr float DIRTY_THRESHOLD   = 20.0f;
constexpr float SLEEPY_THRESHOLD  = 20.0f;
constexpr float HAPPY_AVG         = 70.0f;
constexpr float NEUTRAL_AVG       = 40.0f;

// --- Timing ---
constexpr unsigned long SAVE_INTERVAL_MS  = 60000;  // NVS save period
constexpr unsigned long BOOT_LONGPRESS_MS = 1500;   // long BOOT press = sleep
constexpr unsigned long BUTTON_FLASH_MS   = 200;    // visual tap feedback
constexpr unsigned long PTT_HOLD_MS       = 300;    // hold BOOT this long = talk (else tap=feed)

}  // namespace game
