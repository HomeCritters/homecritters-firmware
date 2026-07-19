#include "Renderer.h"
#include <qrcode.h>
#include "RendererShared.h"

// Renderer partial: config menu pages, pairing overlay, WiFi setup screen
// and their widgets. Same class as Renderer.cpp - split by screen domain.

using namespace theme;
using namespace ui;

// Draws a QR code (version 3, byte mode) centered horizontally at cx.
void Renderer::drawQr(const char* text, int topY, int cx, int quiet) {
  QRCode qr;
  static uint8_t buf[200];  // version 3 needs 107 bytes
  qrcode_initText(&qr, buf, 3, ECC_LOW, text);

  const int scale = 2;
  const int total = (qr.size + quiet * 2) * scale;
  const int ox = cx - total / 2;
  // white background (incl. quiet zone)
  _canvas.fillRect(ox, topY, total, total, TFT_WHITE);
  for (uint8_t y = 0; y < qr.size; y++) {
    for (uint8_t x = 0; x < qr.size; x++) {
      if (qrcode_getModule(&qr, x, y)) {
        _canvas.fillRect(ox + (x + quiet) * scale, topY + (y + quiet) * scale,
                         scale, scale, TFT_BLACK);
      }
    }
  }
}

// Filled rounded button with a centered label (text size 2).
void Renderer::drawPillButton(int x, int y, int w, int h, const char* label, uint16_t bg) {
  _canvas.fillRoundRect(x, y, w, h, 8, bg);
  _canvas.drawRoundRect(x, y, w, h, 8, BTN_BORDER);
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  _canvas.setCursor(x + w / 2 - _canvas.textWidth(label) / 2, y + h / 2 - 7);
  _canvas.print(label);
}

// A labeled +/- stepper with a fill track (shared by volume/LED/screen).
void Renderer::drawStepper(const char* label, int pct, const ui::ButtonSlot& minus,
                           const ui::ButtonSlot& plus) {
  _canvas.setTextSize(1);
  _canvas.setTextColor(menu::TEXT_DIM);
  char t[24];
  snprintf(t, sizeof(t), "%s  %d%%", label, pct);
  _canvas.setCursor(CENTER_X - _canvas.textWidth(t) / 2, minus.cy - 22);
  _canvas.print(t);

  _canvas.fillCircle(minus.cx, minus.cy, MENU_BTN_R, menu::VOL_BTN);
  _canvas.fillRect(minus.cx - 8, minus.cy - 2, 16, 4, TFT_WHITE);
  _canvas.fillCircle(plus.cx, plus.cy, MENU_BTN_R, menu::VOL_BTN);
  _canvas.fillRect(plus.cx - 8, plus.cy - 2, 16, 4, TFT_WHITE);
  _canvas.fillRect(plus.cx - 2, plus.cy - 8, 4, 16, TFT_WHITE);

  const int bx = minus.cx + MENU_BTN_R + 8;
  const int bw = (plus.cx - MENU_BTN_R - 8) - bx;
  _canvas.fillRoundRect(bx, minus.cy - 6, bw, 12, 4, menu::VOL_TRACK);
  _canvas.fillRoundRect(bx, minus.cy - 6, bw * pct / 100, 12, 4, BAR_HIGH);
}

// A speaker box (cabinet) with a tweeter + woofer, centered at (cx,cy). The
// driver centers are punched out in the tile bg so they read as cones.
void Renderer::drawAudioIcon(int cx, int cy, uint16_t bg) {
  _canvas.fillRoundRect(cx - 10, cy - 14, 20, 28, 4, menu::IC_AUDIO);   // cabinet
  _canvas.drawRoundRect(cx - 10, cy - 14, 20, 28, 4, rgb565(35, 80, 160));
  _canvas.fillCircle(cx, cy - 7, 3, menu::IC_AUDIO2);                   // tweeter
  _canvas.fillCircle(cx, cy - 7, 1, bg);
  _canvas.fillCircle(cx, cy + 5, 6, menu::IC_AUDIO2);                   // woofer
  _canvas.fillCircle(cx, cy + 5, 2, bg);
}

// A light bulb: amber glass with a highlight + a gray screw base, at (cx,cy).
void Renderer::drawLightIcon(int cx, int cy, bool on) {
  const uint16_t base = rgb565(150, 150, 160);
  if (on) {
    // Lit: amber glass + highlight + short rays around it.
    _canvas.fillCircle(cx, cy - 4, 9, menu::IC_SUN);       // glass
    _canvas.fillCircle(cx - 3, cy - 7, 3, menu::IC_SUN2);  // highlight
    for (int a = 0; a < 8; a++) {                          // rays
      const float t = a * 3.14159f / 4.0f;
      const int x0 = cx + (int)(11 * cosf(t)), y0 = cy - 4 + (int)(11 * sinf(t));
      const int x1 = cx + (int)(14 * cosf(t)), y1 = cy - 4 + (int)(14 * sinf(t));
      _canvas.drawLine(x0, y0, x1, y1, menu::IC_SUN);
    }
  } else {
    // Off: dark gray glass, outlined, no highlight, no rays.
    _canvas.fillCircle(cx, cy - 4, 9, rgb565(70, 72, 84));
    _canvas.drawCircle(cx, cy - 4, 9, rgb565(120, 124, 140));
  }
  _canvas.fillRect(cx - 5, cy + 5, 10, 3, base);          // neck
  _canvas.fillRoundRect(cx - 4, cy + 8, 8, 6, 2, base);   // screw base
  _canvas.drawFastHLine(cx - 4, cy + 10, 8, menu::CELL_LABEL);  // thread lines
  _canvas.drawFastHLine(cx - 4, cy + 12, 8, menu::CELL_LABEL);
}

// Classic WiFi symbol: three radiating arcs above a dot, green. The arcs are
// plotted point-by-point (a ~110-degree fan) so it doesn't depend on any
// arc-drawing API's angle convention.
void Renderer::drawWifiIcon(int cx, int cy) {
  const uint16_t c = menu::IC_WIFI;
  const int dotx = cx, doty = cy + 10;  // emitter dot near the bottom
  const int radii[3] = {7, 12, 17};
  for (int b = 0; b < 3; b++) {
    for (int deg = -55; deg <= 55; deg += 4) {
      const float t = deg * 3.14159f / 180.0f;
      const int x = dotx + (int)(radii[b] * sinf(t));
      const int y = doty - (int)(radii[b] * cosf(t));
      _canvas.fillCircle(x, y, 1, c);  // ~3px-thick arc
    }
  }
  _canvas.fillCircle(dotx, doty, 2, c);  // emitter dot
}

// Padlock: gold body + gray shackle (Seguranca tile).
void Renderer::drawLockIcon(int cx, int cy) {
  const uint16_t body = rgb565(235, 180, 50), dark = rgb565(150, 110, 20);
  _canvas.fillArc(cx, cy - 3, 5, 8, 180, 360, rgb565(165, 165, 175));  // shackle
  _canvas.fillRoundRect(cx - 9, cy - 3, 18, 15, 3, body);              // body
  _canvas.drawRoundRect(cx - 9, cy - 3, 18, 15, 3, dark);
  _canvas.fillCircle(cx, cy + 3, 2, dark);                             // keyhole
  _canvas.drawFastVLine(cx, cy + 4, 4, dark);
}

// Connections (Conexoes tile): a monitor + a phone, universal "devices"
// glyph. The monitor stand and base are centered UNDER the screen.
void Renderer::drawHouseIcon(int cx, int cy) {
  const uint16_t body = rgb565(210, 218, 230), scr = rgb565(70, 160, 235),
                 dark = rgb565(70, 82, 100);
  const int mx = cx - 5;  // monitor center (leaves room for the phone at right)
  _canvas.fillRoundRect(mx - 10, cy - 10, 20, 15, 2, body);   // bezel
  _canvas.drawRoundRect(mx - 10, cy - 10, 20, 15, 2, dark);
  _canvas.fillRect(mx - 8, cy - 8, 16, 11, scr);              // screen
  _canvas.fillRect(mx - 2, cy + 5, 4, 3, dark);               // stand (centered)
  _canvas.fillRect(mx - 6, cy + 8, 12, 2, dark);              // base (centered)
  // Phone at the right, slightly lower.
  _canvas.fillRoundRect(cx + 7, cy - 4, 9, 15, 2, dark);
  _canvas.fillRect(cx + 9, cy - 1, 5, 9, scr);
  _canvas.fillCircle(cx + 11, cy + 9, 1, body);               // home button
}

// Key (Parear tile): horizontal gold key - bow ring left, straight shaft
// right with two teeth. Reads cleanly at tile size.
void Renderer::drawKeyIcon(int cx, int cy) {
  const uint16_t gold = rgb565(240, 185, 55), dark = rgb565(165, 120, 25);
  _canvas.fillCircle(cx - 8, cy, 7, gold);            // bow (ring)
  _canvas.drawCircle(cx - 8, cy, 7, dark);
  _canvas.fillCircle(cx - 8, cy, 3, menu::CELL_BG);   // ring hole
  _canvas.fillRect(cx - 1, cy - 2, 16, 4, gold);      // shaft
  _canvas.drawFastHLine(cx - 1, cy - 2, 16, dark);    // shaft top edge
  _canvas.fillRect(cx + 11, cy + 2, 3, 6, gold);      // tooth (tip)
  _canvas.fillRect(cx + 5, cy + 2, 3, 4, gold);       // tooth (mid)
}

// Browser window (Portal tile): white page, blue title bar with dots and
// gray content lines - much more readable than the old mini-QR.
void Renderer::drawQrGlyph(int cx, int cy) {
  const uint16_t bar = rgb565(90, 125, 210), line = rgb565(185, 195, 212);
  _canvas.fillRoundRect(cx - 13, cy - 11, 26, 22, 3, TFT_WHITE);  // page
  _canvas.fillRoundRect(cx - 13, cy - 11, 26, 8, 3, bar);         // title bar
  _canvas.fillRect(cx - 13, cy - 6, 26, 3, bar);                  // square bar bottom
  _canvas.drawRoundRect(cx - 13, cy - 11, 26, 22, 3, bar);
  _canvas.fillCircle(cx - 9, cy - 7, 1, TFT_WHITE);               // window dots
  _canvas.fillCircle(cx - 5, cy - 7, 1, TFT_WHITE);
  _canvas.fillRect(cx - 9, cy - 1, 18, 2, line);                  // content lines
  _canvas.fillRect(cx - 9, cy + 3, 13, 2, line);
  _canvas.fillRect(cx - 9, cy + 7, 16, 2, line);
}

// One grid cell: light tile, colored icon on top, label under it.
void Renderer::drawGridCell(int x, int y, const char* label, char icon) {
  _canvas.fillRoundRect(x, y, MENU_CELL_W, MENU_CELL_H, 12, menu::CELL_BG);
  _canvas.drawRoundRect(x, y, MENU_CELL_W, MENU_CELL_H, 12, BTN_BORDER);
  const int cx = x + MENU_CELL_W / 2, iy = y + 26;
  switch (icon) {
    case 'a': drawAudioIcon(cx, iy, menu::CELL_BG); break;
    case 'l': drawLightIcon(cx, iy);                break;
    case 'w': drawWifiIcon(cx, iy);                 break;
    case 's': drawLockIcon(cx, iy);                 break;
    case 'd': drawHouseIcon(cx, iy);                break;  // devices glyph
    case 'k': drawKeyIcon(cx, iy);                  break;
    case 'q': drawQrGlyph(cx, iy);                  break;
  }
  _canvas.setTextColor(menu::CELL_LABEL);
  _canvas.setTextSize(1);
  _canvas.setCursor(cx - _canvas.textWidth(label) / 2, y + MENU_CELL_H - 15);
  _canvas.print(label);
}

void Renderer::drawMenu(ui::MenuPage page, int volume, int ledBright,
                        int batteryPct, bool wifiOn, const char* ip) {
  beginScreen(menu::BG);  // full-screen dark purple background
  if (page == PAGE_AUDIO)       drawMenuAudio(volume);
  else if (page == PAGE_LIGHT)  drawMenuLight(ledBright);
  else if (page == PAGE_CONN)   drawMenuConn(wifiOn, ip);
  else if (page == PAGE_QR)     drawMenuQr(wifiOn, ip);
  else if (page == PAGE_SEC)    drawMenuSec();
  else if (page == PAGE_SEC_HA) drawMenuSecHa();
  else                          drawMenuMain(batteryPct, wifiOn, ip);
}

// Main page: title + battery, a 2x2 grid - Audio/Luz (top), WiFi/QR (bottom).
// The QR tile shows the code and is tappable (opens the QR detail page). + Voltar.
void Renderer::drawMenuMain(int batteryPct, bool wifiOn, const char* ip) {
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* title = "Config";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 6);
  _canvas.print(title);
  drawBatteryPill(26, batteryPct, TFT_WHITE, menu::TEXT_DIM);

  drawGridCell(MENU_COL_L, MENU_ROW_1, "Audio", 'a');
  drawGridCell(MENU_COL_R, MENU_ROW_1, "Luz", 'l');
  drawGridCell(MENU_COL_L, MENU_ROW_2, "Conexao", 'w');
  drawGridCell(MENU_COL_R, MENU_ROW_2, "Seguranca", 's');

  drawLeftHandle();  // pull (or tap) the left tab to close the menu
}

// Conexao: WiFi setup + Portal (QR). Shows the connection status up top.
void Renderer::drawMenuConn(bool wifiOn, const char* ip) {
  char status[48];
  if (wifiOn && ip && ip[0]) snprintf(status, sizeof(status), "Conectado: %s", ip);
  else strlcpy(status, "Sem WiFi", sizeof(status));
  drawPageHeader("Conexao", status);

  drawGridCell(MENU_COL_L, MENU_SUB_ROW, "WiFi", 'w');
  drawGridCell(MENU_COL_R, MENU_SUB_ROW, "Portal", 'q');
  drawLeftHandle();
}

// Centered page title + subtitle at a y that clears the round bezel: a
// full-width title at the very top (y=14) gets clipped where the circle
// narrows, so titles start lower where the chord is wide enough.
void Renderer::drawPageHeader(const char* title, const char* sub) {
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 28);
  _canvas.print(title);
  _canvas.setTextSize(1);
  _canvas.setTextColor(menu::TEXT_DIM);
  _canvas.setCursor(CENTER_X - _canvas.textWidth(sub) / 2, 52);
  _canvas.print(sub);
}

// Seguranca: Dispositivos (paired clients) + Parear (PIN).
void Renderer::drawMenuSec() {
  drawPageHeader("Seguranca", "Pareamento e acesso");
  drawGridCell(MENU_COL_L, MENU_SUB_ROW, "Conexoes", 'd');
  drawGridCell(MENU_COL_R, MENU_SUB_ROW, "Parear", 'k');
  drawLeftHandle();
}

// Seguranca > Dispositivos: who is paired right now (authed clients, IP list).
void Renderer::drawMenuSecHa() {
  drawPageHeader("Conexoes", "Conectados agora (HA / portal)");

  // One centered line per client IP ('\n'-separated summary from main).
  _canvas.setTextColor(TFT_WHITE);
  int y = 80;
  const char* p = _clientsInfo;
  while (*p && y < 160) {
    const char* nl = strchr(p, '\n');
    const int len = nl ? (int)(nl - p) : (int)strlen(p);
    char line[40];
    snprintf(line, sizeof(line), "%.*s", len, p);
    _canvas.setCursor(CENTER_X - _canvas.textWidth(line) / 2, y);
    _canvas.print(line);
    y += 16;
    p += len + (nl ? 1 : 0);
    if (!nl) break;
  }

  // Revoke-all button (two-tap confirm): rotates the token and kicks every
  // paired client - they all have to PIN-pair again.
  const uint16_t red = _revokeArmed ? rgb565(230, 60, 50) : rgb565(120, 35, 35);
  _canvas.fillRoundRect(REVOKE_X, REVOKE_Y, REVOKE_W, REVOKE_H, 8, red);
  _canvas.drawRoundRect(REVOKE_X, REVOKE_Y, REVOKE_W, REVOKE_H, 8, rgb565(255, 120, 110));
  const char* lbl = _revokeArmed ? "Confirma?" : "Revogar acesso";
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setCursor(CENTER_X - _canvas.textWidth(lbl) / 2, REVOKE_Y + 11);
  _canvas.print(lbl);
  drawLeftHandle();
}

// Seguranca > Senha: THE pairing token - the only place it is ever shown
// (screen = physical access = trusted). Portal and HA ask for it once.
// Full-screen pairing overlay: pops automatically when a client asks to pair
// (or via Seguranca > Parear). Six digit boxes, TV-pairing style.
void Renderer::drawPairingOverlay() {
  beginScreen(menu::BG);
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* title = "Pareamento";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 26);
  _canvas.print(title);

  // Six digit boxes centered: 30px boxes with 4px gaps (6*30+5*4 = 200px).
  const int bw = 30, bh = 40, gap = 4;
  const int x0 = CENTER_X - (6 * bw + 5 * gap) / 2;
  const int y0 = 92;
  _canvas.setTextSize(3);
  for (int i = 0; i < 6 && _pairPin[i]; i++) {
    const int x = x0 + i * (bw + gap);
    _canvas.fillRoundRect(x, y0, bw, bh, 6, menu::CELL_BG);
    _canvas.drawRoundRect(x, y0, bw, bh, 6, menu::AP_NAME);
    _canvas.setTextColor(menu::BG);
    // Optical centering: the 6x8 font carries a 1px trailing gap baked into
    // the glyph (15px of ink in an 18px cell at size 3), so a "mathematical"
    // center sits visibly left. +2 rebalances; same idea vertically (21px of
    // ink in a 24px cell).
    _canvas.setCursor(x + (bw - 18) / 2 + 2, y0 + (bh - 21) / 2);
    _canvas.print(_pairPin[i]);
  }

  _canvas.setTextSize(1);
  _canvas.setTextColor(menu::TEXT_DIM);
  const char* l1 = "Digite este codigo no app";
  const char* l2 = "para conectar (90s).";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(l1) / 2, 152);
  _canvas.print(l1);
  _canvas.setCursor(CENTER_X - _canvas.textWidth(l2) / 2, 164);
  _canvas.print(l2);

  // "Voltar" button - closes the pairing window.
  _canvas.fillRoundRect(PAIR_CANCEL_X, PAIR_CANCEL_Y, PAIR_CANCEL_W, PAIR_CANCEL_H, 8,
                        menu::CELL_BG);
  _canvas.drawRoundRect(PAIR_CANCEL_X, PAIR_CANCEL_Y, PAIR_CANCEL_W, PAIR_CANCEL_H, 8,
                        rgb565(200, 90, 90));
  _canvas.setTextSize(2);
  _canvas.setTextColor(rgb565(200, 60, 60));
  const char* lbl = "Voltar";
  // Optical centering: ink is textWidth minus the trailing 2px gap (size 2).
  _canvas.setCursor(CENTER_X - (_canvas.textWidth(lbl) - 2) / 2,
                    PAIR_CANCEL_Y + (PAIR_CANCEL_H - 14) / 2);
  _canvas.print(lbl);
}

// QR detail page: the code (large) + what it is + how to use it + the URLs.
void Renderer::drawMenuQr(bool wifiOn, const char* ip) {
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* title = "Portal";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 8);
  _canvas.print(title);

  _canvas.setTextSize(1);
  _canvas.setTextColor(menu::TEXT_DIM);
  if (wifiOn && ip && ip[0]) {
    char qrUrl[40];
    snprintf(qrUrl, sizeof(qrUrl), "http://%s/", ip);
    drawQr(qrUrl, 26);  // centered, ~70px
    const char* l1 = "Aponte a camera do celular";
    const char* l2 = "para abrir o painel.";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(l1) / 2, 108);
    _canvas.print(l1);
    _canvas.setCursor(CENTER_X - _canvas.textWidth(l2) / 2, 122);
    _canvas.print(l2);
    char urlIp[40];
    snprintf(urlIp, sizeof(urlIp), "http://%s", ip);
    _canvas.setTextColor(menu::AP_NAME);
    _canvas.setCursor(CENTER_X - _canvas.textWidth("http://critter.local") / 2, 146);
    _canvas.print("http://critter.local");
    _canvas.setCursor(CENTER_X - _canvas.textWidth(urlIp) / 2, 160);
    _canvas.print(urlIp);
  } else {
    const char* m1 = "Conecte o WiFi primeiro";
    const char* m2 = "para gerar o QR do painel.";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(m1) / 2, 96);
    _canvas.print(m1);
    _canvas.setCursor(CENTER_X - _canvas.textWidth(m2) / 2, 110);
    _canvas.print(m2);
  }

  drawLeftHandle();  // pull (or tap) the left tab to go back
}

void Renderer::drawMenuAudio(int volume) {
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* title = "Audio";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 14);
  _canvas.print(title);

  drawStepper("Volume", volume, MENU_VOL_MINUS, MENU_VOL_PLUS);
  drawLeftHandle();  // pull (or tap) the left tab to go back
}

void Renderer::drawMenuLight(int ledBright) {
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* title = "Luz";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 14);
  _canvas.print(title);

  drawStepper("LED", ledBright, MENU_LED_MINUS, MENU_LED_PLUS);
  drawStepper("Tela", _scrBright, MENU_SCR_MINUS, MENU_SCR_PLUS);
  drawLeftHandle();  // pull (or tap) the left tab to go back
}

void Renderer::drawWifiConfig(const char* apName) {
  beginScreen(menu::BG);
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* t = "Config WiFi";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(t) / 2, 34);
  _canvas.print(t);

  _canvas.setTextSize(1);
  _canvas.setTextColor(menu::TEXT_DIM);
  const char* l1 = "No celular, conecte na rede:";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(l1) / 2, 78);
  _canvas.print(l1);

  _canvas.setTextSize(2);
  _canvas.setTextColor(menu::AP_NAME);
  _canvas.setCursor(CENTER_X - _canvas.textWidth(apName) / 2, 98);
  _canvas.print(apName);

  _canvas.setTextSize(1);
  _canvas.setTextColor(menu::TEXT_DIM);
  const char* l2 = "e siga o portal que abrir.";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(l2) / 2, 132);
  _canvas.print(l2);

  drawLeftHandle();  // pull (or tap) the left tab to exit config
  endScreen();
}

// A small battery pill (icon + %). Placed high but INSIDE the visible circle
// (the corners are hidden behind the round bezel). Reused by the pet scene and
// the config menu; `outline`/`txt` adapt it to each background.
void Renderer::drawBatteryPill(int topY, int pct, uint16_t outline, uint16_t txt) {
  const uint16_t lvl = pct <= 20 ? rgb565(230, 70, 60)
                     : pct <= 50 ? rgb565(240, 190, 60)
                                 : rgb565(90, 200, 110);
  char t[6];
  snprintf(t, sizeof(t), "%d%%", pct);
  _canvas.setTextSize(1);
  const int tw = _canvas.textWidth(t);
  const int total = 20 + 3 + tw;  // 18px body + 2px nub + gap + text
  const int bx = CENTER_X - total / 2, by = topY;
  _canvas.drawRoundRect(bx, by, 18, 9, 2, outline);
  _canvas.fillRect(bx + 18, by + 3, 2, 3, outline);            // nub
  _canvas.fillRect(bx + 2, by + 2, (14 * pct) / 100, 5, lvl);  // level
  _canvas.setTextColor(txt);
  _canvas.setCursor(bx + 20 + 3, by + 1);
  _canvas.print(t);
}
