#pragma once
#include <cstdint>

// ============================================================
// Shared drawing helpers for the Renderer partials. The Renderer
// class is implemented across several .cpp files (one per screen
// domain: scene, menus, HA panel, weather screen, games); the
// bits they all lean on live here.
// ============================================================

// Forest ground line (where the grass meets the sky / treeline).
static constexpr int GROUND_Y = 108;

// Lerp two RGB565 colors (t in 0..1). Sky gradients, tints, fades.
static inline uint16_t lerp565(uint16_t a, uint16_t b, float t) {
  int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  int r = ar + (int)((br - ar) * t);
  int g = ag + (int)((bg - ag) * t);
  int bl = ab + (int)((bb - ab) * t);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}
