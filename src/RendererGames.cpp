#include "Renderer.h"
#include "GameConfig.h"
#include "RendererShared.h"
#include "ferret_game.h"  // small ferret sprite for the mini-games

// Renderer partial: games menu + Jump! + Bolinha + Genius screens.

using namespace theme;
using namespace ui;

// A game tile: light square with a colored icon on top and a label under it.
void Renderer::drawGameTile(int x, int y, const char* label, char icon, uint16_t iconColor) {
  _canvas.fillRoundRect(x, y, GAME_TILE_W, GAME_TILE_H, 12, menu::CELL_BG);
  _canvas.drawRoundRect(x, y, GAME_TILE_W, GAME_TILE_H, 12, BTN_BORDER);
  const int cx = x + GAME_TILE_W / 2, cy = y + 26;  // config-cell icon center
  if (icon == 's') {  // Genius: the 4-color wheel
    _canvas.fillArc(cx, cy, 16, 6, 182, 268, rgb565(60, 200, 80));   // TL green
    _canvas.fillArc(cx, cy, 16, 6, 272, 358, rgb565(230, 60, 50));   // TR red
    _canvas.fillArc(cx, cy, 16, 6, 92, 178, rgb565(235, 210, 50));   // BL yellow
    _canvas.fillArc(cx, cy, 16, 6, 2, 88, rgb565(70, 120, 235));     // BR blue
  } else if (icon == 'j') {  // Doodle Jump: the doodler bouncing on a platform
    const uint16_t plat = rgb565(45, 140, 70);  // platform (darker green)
    _canvas.fillRoundRect(cx - 16, cy + 13, 32, 6, 3, plat);
    _canvas.fillTriangle(cx - 4, cy - 15, cx + 4, cy - 15, cx, cy - 20, iconColor);  // jump arrow
    _canvas.fillRoundRect(cx - 10, cy - 9, 20, 18, 6, iconColor);   // body
    _canvas.fillRect(cx - 7, cy + 8, 4, 6, iconColor);              // feet
    _canvas.fillRect(cx + 3, cy + 8, 4, 6, iconColor);
    _canvas.fillCircle(cx - 4, cy - 3, 3, TFT_WHITE);               // eyes
    _canvas.fillCircle(cx + 4, cy - 3, 3, TFT_WHITE);
    _canvas.fillCircle(cx - 4, cy - 3, 1, menu::CELL_LABEL);
    _canvas.fillCircle(cx + 4, cy - 3, 1, menu::CELL_LABEL);
  } else {            // Bolinha: a yellow tennis ball with a curved seam
    _canvas.fillCircle(cx, cy, 16, iconColor);
    _canvas.drawCircle(cx, cy, 16, rgb565(150, 175, 40));  // rim
    for (int dy = -14; dy <= 14; dy++) {                   // hourglass seam
      const float f = 1.0f - (float)(dy * dy) / (14.0f * 14.0f);
      const int off = (int)(7 * f);
      _canvas.fillCircle(cx - 11 + off, cy + dy, 1, TFT_WHITE);
      _canvas.fillCircle(cx + 11 - off, cy + dy, 1, TFT_WHITE);
    }
  }
  _canvas.setTextColor(menu::CELL_LABEL);
  _canvas.setTextSize(1);
  _canvas.setCursor(cx - _canvas.textWidth(label) / 2, y + GAME_TILE_H - 15);
  _canvas.print(label);
}

void Renderer::drawGamesMenu() {
  beginScreen(menu::BG);
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* title = "Jogos";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 14);
  _canvas.print(title);

  drawGameTile(GAME_COL_L, GAME_ROW_1, "Jump!", 'j', menu::IC_DOODLE);
  drawGameTile(GAME_COL_R, GAME_ROW_1, "Bolinha", 'b', menu::IC_BALL);
  drawGameTile(GAME_COL_C, GAME_ROW_2, "Genius", 's', 0);

  drawLeftHandle();  // pull (or tap) the left tab to go back to the pet
  endScreen();
}

// The official ferret sprite (game-sized), jump animation, centered at (cx,cy).
void Renderer::drawDoodleFerret(int cx, int cy, bool faceLeft) {
  const int idx = (millis() / 80) % FERRET_G_JUMP_FRAMES;
  const uint16_t* fr = faceLeft ? &ferret_g_jump_l[idx][0] : &ferret_g_jump[idx][0];
  // Cast the key to uint16_t so the same (transparency-applying) pushImage
  // overload is picked as the scene ferret - a bare 0xF81F macro is an int
  // literal and selects a variant that leaves the magenta background opaque.
  _canvas.pushImage(cx - FERRET_G_FRAME_W / 2, cy - FERRET_G_FRAME_H / 2,
                    FERRET_G_FRAME_W, FERRET_G_FRAME_H, fr,
                    (uint16_t)FERRET_G_TRANSPARENT_KEY);
}

void Renderer::drawDoodle(DoodleGame& game) {
  beginScreen(rgb565(150, 205, 235));  // light sky

  // parallax clouds: drift down slower than the platforms as the run climbs
  static const int16_t clouds[3][2] = {{52, 34}, {182, 96}, {96, 168}};
  const int coff = (int)(game.climbPx() * 0.35f);
  const uint16_t cloudCol = rgb565(226, 240, 250);
  for (auto& c : clouds) {
    const int cy = ((c[1] + coff) % 280) - 20;
    _canvas.fillCircle(c[0], cy, 10, cloudCol);
    _canvas.fillCircle(c[0] - 12, cy + 3, 7, cloudCol);
    _canvas.fillCircle(c[0] + 12, cy + 3, 7, cloudCol);
  }

  const auto* plats = game.platforms();
  for (int i = 0; i < DoodleGame::PLAT_COUNT; i++) {
    const auto& p = plats[i];
    if (!p.active) continue;
    const int px = (int)p.x, py = (int)p.y;
    // Color says type: green = solid, blue = sliding, beige+crack = crumble.
    uint16_t fill = rgb565(70, 175, 80), edge = rgb565(45, 120, 55);
    if (p.vx != 0)       { fill = rgb565(70, 150, 200); edge = rgb565(40, 95, 140); }
    else if (p.crumble)  { fill = rgb565(215, 190, 145); edge = rgb565(150, 125, 85); }
    _canvas.fillRoundRect(px, py, DoodleGame::PLAT_W, DoodleGame::PLAT_H, 3, fill);
    _canvas.drawRoundRect(px, py, DoodleGame::PLAT_W, DoodleGame::PLAT_H, 3, edge);
    if (p.crumble) {  // crack marks
      const int mx = px + DoodleGame::PLAT_W / 2;
      _canvas.drawLine(mx - 4, py + 1, mx, py + DoodleGame::PLAT_H - 2, edge);
      _canvas.drawLine(mx, py + DoodleGame::PLAT_H - 2, mx + 5, py + 1, edge);
    }
    if (p.spring) {  // spring boost marker on top of the platform
      const int sx = px + DoodleGame::PLAT_W / 2 - 4, sy = py - 6;
      _canvas.fillRect(sx, sy, 8, 6, rgb565(235, 150, 40));
      _canvas.drawFastHLine(sx, sy + 1, 8, rgb565(120, 70, 20));
      _canvas.drawFastHLine(sx, sy + 4, 8, rgb565(120, 70, 20));
    }
  }

  drawDoodleFerret((int)game.ferretX(), (int)game.ferretY() + 6, game.faceLeft());

  // score (top center) + record (top right, small)
  _canvas.setTextColor(rgb565(30, 50, 70));
  _canvas.setTextSize(2);
  char sc[12];
  snprintf(sc, sizeof(sc), "%d", game.score());
  _canvas.setCursor(CENTER_X - _canvas.textWidth(sc) / 2, 8);
  _canvas.print(sc);
  if (game.hiScore() > 0) {
    _canvas.setTextSize(1);
    char hs[16];
    snprintf(hs, sizeof(hs), "rec %d", game.hiScore());
    _canvas.setCursor(CENTER_X - _canvas.textWidth(hs) / 2, 26);
    _canvas.print(hs);
  }

  // back button (top-left)
  _canvas.fillCircle(18, 16, 12, BTN_BG);
  _canvas.drawCircle(18, 16, 12, BTN_BORDER);
  _canvas.fillTriangle(22, 11, 22, 21, 14, 16, BTN_BORDER);

  if (game.gameOver()) {
    _canvas.fillRoundRect(34, 86, 172, 72, 10, menu::CLOCK_BG);
    _canvas.drawRoundRect(34, 86, 172, 72, 10, menu::CLOCK_EDGE);
    _canvas.setTextColor(TFT_WHITE);
    _canvas.setTextSize(2);
    const char* go = "Fim!";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(go) / 2, 94);
    _canvas.print(go);
    _canvas.setTextSize(1);
    _canvas.setTextColor(menu::CLOCK_DATE);
    char l[24];
    snprintf(l, sizeof(l), "Score: %d", game.score());
    _canvas.setCursor(CENTER_X - _canvas.textWidth(l) / 2, 116);
    _canvas.print(l);
    if (game.newRecord()) {
      _canvas.setTextColor(rgb565(250, 210, 60));
      const char* nr = "NOVO RECORDE!";
      _canvas.setCursor(CENTER_X - _canvas.textWidth(nr) / 2, 130);
      _canvas.print(nr);
      _canvas.setTextColor(menu::CLOCK_DATE);
    } else {
      char r[24];
      snprintf(r, sizeof(r), "Recorde: %d", game.hiScore());
      _canvas.setCursor(CENTER_X - _canvas.textWidth(r) / 2, 130);
      _canvas.print(r);
    }
    const char* h = "toque p/ voltar";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(h) / 2, 144);
    _canvas.print(h);
  }

  endScreen();
}

// The game ferret with a mode-appropriate animation, centered at (cx,cy).
// zoom>1 nearest-scales the 40px sprite (used at 2x in Bolinha).
void Renderer::drawGameFerret(int cx, int cy, int mode, bool faceLeft, float zoom) {
  const uint16_t* fr;
  if (mode == 2) {        // jumping (celebration)
    const int idx = (millis() / 80) % FERRET_G_JUMP_FRAMES;
    fr = faceLeft ? &ferret_g_jump_l[idx][0] : &ferret_g_jump[idx][0];
  } else if (mode == 1) { // walking (chase / stroll)
    const int idx = (millis() / 90) % FERRET_G_WALK_FRAMES;
    fr = faceLeft ? &ferret_g_walk_l[idx][0] : &ferret_g_walk[idx][0];
  } else {                // idle (breathing loop)
    const int idx = (millis() / 180) % FERRET_G_IDLE_FRAMES;
    fr = faceLeft ? &ferret_g_idle_l[idx][0] : &ferret_g_idle[idx][0];
  }
  if (zoom <= 1.01f) {
    _canvas.pushImage(cx - FERRET_G_FRAME_W / 2, cy - FERRET_G_FRAME_H / 2,
                      FERRET_G_FRAME_W, FERRET_G_FRAME_H, fr,
                      (uint16_t)FERRET_G_TRANSPARENT_KEY);
  } else {
    _canvas.pushImageRotateZoom(cx, cy, FERRET_G_FRAME_W / 2.0f, FERRET_G_FRAME_H / 2.0f,
                                0.0f, zoom, zoom,
                                FERRET_G_FRAME_W, FERRET_G_FRAME_H, fr,
                                (uint16_t)FERRET_G_TRANSPARENT_KEY);
  }
}

// A tennis ball: yellow disc + rim + the white curved seam.
void Renderer::drawTennisBall(int cx, int cy, int r) {
  _canvas.fillCircle(cx, cy, r, menu::IC_BALL);
  _canvas.drawCircle(cx, cy, r, rgb565(150, 175, 40));
  const int span = r - 2;
  for (int dy = -span; dy <= span; dy++) {  // hourglass seam ")("
    const float f = 1.0f - (float)(dy * dy) / (float)(span * span);
    const int off = (int)((r * 0.45f) * f);
    _canvas.drawPixel(cx - (r - 3) + off, cy + dy, TFT_WHITE);
    _canvas.drawPixel(cx + (r - 3) - off, cy + dy, TFT_WHITE);
  }
}

void Renderer::drawBall(BallGame& game) {
  // Reuse the day forest scene (sky, sun, cabin, pines, grass) as the backdrop.
  _p = DAY;
  drawSky();
  drawSun();
  drawForest(false);
  drawSparkles(false);

  // ferret at the home height (2x = 80px, feet on the grass), then the ball
  const int mode = game.state() == BallGame::CELEBRATE ? 2 : (game.walking() ? 1 : 0);
  drawGameFerret((int)game.ferretX(), 88, mode, game.faceLeft(), 2);
  drawTennisBall((int)game.ballX(), (int)game.ballY(), 8);

  // catch count (top center) on a small dark chip so it reads over the scene
  char sc[12];
  snprintf(sc, sizeof(sc), "%d", game.catches());
  _canvas.setTextSize(2);
  const int scw = _canvas.textWidth(sc);
  _canvas.fillRoundRect(CENTER_X - scw / 2 - 6, 6, scw + 12, 20, 5, menu::CLOCK_BG);
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setCursor(CENTER_X - scw / 2, 9);
  _canvas.print(sc);

  drawLeftHandle();  // pull right (or tap) to exit

  if (game.ready()) {
    _canvas.setTextSize(1);
    const char* hint = "arraste p/ lancar";
    const int hw = _canvas.textWidth(hint);
    _canvas.fillRoundRect(CENTER_X - hw / 2 - 5, 201, hw + 10, 13, 4, menu::CLOCK_BG);
    _canvas.setTextColor(TFT_WHITE);
    _canvas.setCursor(CENTER_X - hw / 2, 204);
    _canvas.print(hint);
  }

  endScreen();
}

void Renderer::drawSimon(SimonGame& game) {
  beginScreen(menu::BG);

  // Four diagonal quadrant pads on a ring (classic Genius layout).
  // Index: 0=TL green, 1=TR red, 2=BL yellow, 3=BR blue.
  static const uint16_t DIM[4] = {
    rgb565(22, 92, 38), rgb565(115, 28, 26), rgb565(120, 102, 18), rgb565(24, 48, 115)};
  static const uint16_t LIT[4] = {
    rgb565(70, 240, 100), rgb565(255, 70, 55), rgb565(255, 225, 50), rgb565(80, 140, 255)};
  // LovyanGFX angles: 0 deg at 3 o'clock, clockwise. 4-deg gaps between pads.
  static const int16_t A0[4] = {182, 272, 92, 2};
  static const int16_t A1[4] = {268, 358, 178, 88};
  for (int i = 0; i < 4; i++) {
    _canvas.fillArc(CENTER_X, CENTER_Y, SIMON_RING_OUTER, SIMON_RING_INNER,
                    A0[i], A1[i], game.litColor() == i ? LIT[i] : DIM[i]);
  }

  // Center: big score up top, then the pet hopping, a hint and the exit button.
  char sc[8];
  snprintf(sc, sizeof(sc), "%d", game.score());
  _canvas.setTextSize(3);
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setCursor(CENTER_X - _canvas.textWidth(sc) / 2, 44);
  _canvas.print(sc);

  // The pet hops in place (jump sprite while airborne). Bigger + centered.
  const unsigned long t = millis() % 900;
  int hop = 0, mode = 0;
  if (t < 520) { hop = (int)(sinf((t / 520.0f) * 3.14159f) * 18.0f); mode = 2; }
  drawGameFerret(CENTER_X, 104 - hop, mode, false, 1.6f);

  _canvas.setTextSize(2);
  _canvas.setTextColor(menu::TEXT_DIM);
  const char* hint = game.state() == SimonGame::SHOWING ? "Observe..."
                   : game.state() == SimonGame::WAITING ? "Sua vez!" : "";
  if (*hint) {
    _canvas.setCursor(CENTER_X - _canvas.textWidth(hint) / 2, 140);
    _canvas.print(hint);
  }

  // exit button (x) at the bottom of the inner circle
  _canvas.fillCircle(CENTER_X, SIMON_EXIT_CY, 15, rgb565(62, 50, 92));
  _canvas.drawCircle(CENTER_X, SIMON_EXIT_CY, 15, BTN_BORDER);
  for (int o = -1; o <= 0; o++) {  // 2px-thick "x"
    _canvas.drawLine(CENTER_X - 6 + o, SIMON_EXIT_CY - 6, CENTER_X + 6 + o, SIMON_EXIT_CY + 6, TFT_WHITE);
    _canvas.drawLine(CENTER_X + 6 + o, SIMON_EXIT_CY - 6, CENTER_X - 6 + o, SIMON_EXIT_CY + 6, TFT_WHITE);
  }

  if (game.gameOver()) {
    _canvas.fillRoundRect(34, 86, 172, 72, 10, menu::CLOCK_BG);
    _canvas.drawRoundRect(34, 86, 172, 72, 10, menu::CLOCK_EDGE);
    _canvas.setTextColor(TFT_WHITE);
    _canvas.setTextSize(2);
    const char* go = "Errou!";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(go) / 2, 94);
    _canvas.print(go);
    _canvas.setTextSize(1);
    _canvas.setTextColor(menu::CLOCK_DATE);
    char l[24];
    snprintf(l, sizeof(l), "Pontos: %d", game.score());
    _canvas.setCursor(CENTER_X - _canvas.textWidth(l) / 2, 116);
    _canvas.print(l);
    if (game.newRecord()) {
      _canvas.setTextColor(rgb565(250, 210, 60));
      const char* nr = "NOVO RECORDE!";
      _canvas.setCursor(CENTER_X - _canvas.textWidth(nr) / 2, 130);
      _canvas.print(nr);
      _canvas.setTextColor(menu::CLOCK_DATE);
    } else {
      char r[24];
      snprintf(r, sizeof(r), "Recorde: %d", game.hiScore());
      _canvas.setCursor(CENTER_X - _canvas.textWidth(r) / 2, 130);
      _canvas.print(r);
    }
    const char* h = "toque p/ sair";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(h) / 2, 144);
    _canvas.print(h);
  }

  endScreen();
}
