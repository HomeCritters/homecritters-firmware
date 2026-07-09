#pragma once
#include <cstdint>
// ============================================================
// UI color palette, centralized. The scene has two themes that
// switch with the pet state:
//   - AWAKE    -> DAY   (turquoise sky, SUN)
//   - SLEEPING -> NIGHT (magic purple sky, MOON + stars)
// The Renderer picks the ScenePalette based on pet.sleeping().
// ============================================================

namespace theme {

// constexpr helper: RGB (8/8/8) -> RGB565, for readable color literals.
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Colors of the MAGIC FOREST scene that change between day and night.
struct ScenePalette {
  uint16_t skyTop;      // top of the sky (gradient)
  uint16_t skyBottom;   // sky near the horizon
  uint16_t treeFar;     // distant treeline (silhouette)
  uint16_t ground;      // grass
  uint16_t groundDark;  // grass shading/tufts
  uint16_t treeNear;    // foreground tree foliage
  uint16_t treeTrunk;   // trunk
  uint16_t cabinWall;   // log cabin wall
  uint16_t cabinRoof;   // roof
  uint16_t cabinWindow; // window (lit at night)
  uint16_t sparkle;     // magic dust / fireflies
  uint16_t text;        // primary text (name)
  uint16_t textDim;     // secondary text / labels / battery
  uint16_t barBg;       // stat bar background
};

constexpr ScenePalette DAY = {
  rgb565(120, 195, 220),  // skyTop   - daytime turquoise
  rgb565(205, 230, 210),  // skyBottom
  rgb565(95, 140, 100),   // treeFar  - hazy green in the distance
  rgb565(85, 155, 70),    // ground   - vivid grass
  rgb565(60, 120, 55),    // groundDark
  rgb565(45, 100, 55),    // treeNear - dark foliage
  rgb565(95, 65, 45),     // treeTrunk
  rgb565(140, 95, 60),    // cabinWall - wood
  rgb565(95, 60, 42),     // cabinRoof
  rgb565(90, 70, 45),     // cabinWindow (unlit by day)
  rgb565(255, 250, 205),  // sparkle
  rgb565(25, 40, 30),     // text (dark on light sky)
  rgb565(55, 80, 60),     // textDim
  rgb565(210, 220, 205),  // barBg (light)
};

constexpr ScenePalette AFTERNOON = {
  rgb565(210, 110, 120),  // skyTop   - dusky rose (golden hour)
  rgb565(255, 180, 110),  // skyBottom - warm orange glow near the horizon
  rgb565(120, 105, 90),   // treeFar  - warm hazy
  rgb565(110, 125, 65),   // ground   - golden grass
  rgb565(80, 95, 48),     // groundDark
  rgb565(60, 70, 42),     // treeNear
  rgb565(90, 60, 42),     // treeTrunk
  rgb565(150, 100, 62),   // cabinWall
  rgb565(100, 60, 42),    // cabinRoof
  rgb565(120, 90, 55),    // cabinWindow (unlit, warm wood)
  rgb565(255, 240, 200),  // sparkle
  rgb565(55, 30, 30),     // text (dark on warm sky)
  rgb565(100, 65, 55),    // textDim
  rgb565(220, 195, 175),  // barBg (warm light)
};

constexpr ScenePalette NIGHT = {
  rgb565(25, 15, 55),     // skyTop   - deep magic purple
  rgb565(70, 45, 110),    // skyBottom - lit purple
  rgb565(35, 30, 62),     // treeFar  - purple-green silhouette
  rgb565(32, 55, 48),     // ground   - dark grass
  rgb565(20, 38, 34),     // groundDark
  rgb565(20, 32, 38),     // treeNear - near black
  rgb565(48, 38, 42),     // treeTrunk
  rgb565(75, 58, 52),     // cabinWall
  rgb565(48, 36, 38),     // cabinRoof
  rgb565(255, 205, 110),  // cabinWindow (LIT - warm glow)
  rgb565(180, 230, 255),  // sparkle (cool fireflies / magic dust)
  rgb565(238, 235, 250),  // text (light on dark sky)
  rgb565(165, 160, 195),  // textDim
  rgb565(48, 45, 72),     // barBg (dark)
};

// --- Celestial bodies ---
constexpr uint16_t SUN         = rgb565(255, 210, 90);
constexpr uint16_t SUN_GLOW    = rgb565(255, 235, 160);
constexpr uint16_t SUNSET      = rgb565(255, 115, 55);   // low setting sun
constexpr uint16_t SUNSET_GLOW = rgb565(255, 165, 90);
constexpr uint16_t MOON        = rgb565(235, 235, 215);
constexpr uint16_t MOON_CRATER = rgb565(205, 205, 185);
constexpr uint16_t MOON_GLOW   = rgb565(60, 70, 110);
constexpr uint16_t STAR        = rgb565(230, 235, 255);

// --- Stat bar fill (same colors day and night) ---
constexpr uint16_t BAR_HIGH    = rgb565(70, 200, 90);
constexpr uint16_t BAR_MID     = rgb565(240, 200, 70);
constexpr uint16_t BAR_LOW     = rgb565(230, 70, 60);

// --- Action buttons ---
constexpr uint16_t BTN_BG          = rgb565(240, 240, 245);
constexpr uint16_t BTN_BG_PRESSED  = rgb565(180, 185, 200);
constexpr uint16_t BTN_BORDER      = rgb565(120, 125, 145);

// --- Button icons ---
constexpr uint16_t ICON_APPLE      = rgb565(220, 60, 55);
constexpr uint16_t ICON_LEAF       = rgb565(60, 160, 70);
constexpr uint16_t ICON_PAW        = rgb565(240, 130, 150);
constexpr uint16_t ICON_MOON       = rgb565(245, 210, 90);
constexpr uint16_t ICON_DROP       = rgb565(70, 150, 230);
constexpr uint16_t ICON_DROP_HL    = rgb565(180, 220, 255);

// --- Full-screen config menu / WiFi setup screens ---
namespace menu {
  constexpr uint16_t BG          = rgb565(26, 18, 48);   // dark purple
  constexpr uint16_t TEXT_DIM    = rgb565(190, 180, 220);
  constexpr uint16_t VOL_BTN     = rgb565(90, 74, 150);
  constexpr uint16_t VOL_TRACK   = rgb565(60, 52, 90);
  constexpr uint16_t WIFI_BTN    = rgb565(70, 150, 230);
  constexpr uint16_t CLOSE_BTN   = rgb565(120, 90, 170);
  constexpr uint16_t EXIT_BTN    = rgb565(200, 80, 80);
  constexpr uint16_t AP_NAME     = rgb565(120, 220, 140);
  constexpr uint16_t CLOCK_BG    = rgb565(22, 16, 44);
  constexpr uint16_t CLOCK_EDGE  = rgb565(95, 85, 140);
  constexpr uint16_t CLOCK_DATE  = rgb565(185, 180, 215);
}

// --- Mood LED colors (low brightness; GRB applied by StatusLed) ---
namespace led {
  constexpr uint8_t HAPPY[3]   = {0, 60, 0};    // green
  constexpr uint8_t NEUTRAL[3] = {40, 40, 0};   // yellow
  constexpr uint8_t SAD[3]     = {50, 15, 0};   // orange
  constexpr uint8_t HUNGRY[3]  = {60, 0, 0};    // red
  constexpr uint8_t SLEEPY[3]  = {0, 0, 40};    // blue
  constexpr uint8_t DIRTY[3]   = {45, 30, 0};   // amber
}

}  // namespace theme
