#!/usr/bin/env python3
"""Accessory sprites for Leon, drawn IN THE BASE SPRITE'S STYLE.

The ferret is 32px pixel art scaled 2.5x (chunky pixels). Accessories drawn
procedurally at native 240p resolution have a finer grain and clash - so the
bed, blanket and every hat are authored here as 1x pixel maps (ASCII art on
the same 32px grid) and exported at the same 2.5x NEAREST scale, sharing the
magenta transparency key. One header comes out of this:

  include/accessory_sprites.h
      - RGB565 arrays (bed, blanket, hats + mirrored hat variants)
      - per-frame HEAD-TOP anchors scanned from ferret-sprite-sheet.png
        (topmost opaque pixels of each frame -> the hat rides the head in
        every animation frame, no hand-tuned offsets)
      - accessoryAnchor(anim, faceLeft, frame, &ax, &ay) lookup

Usage:  python3 assets/accessory_sprites.py   (PlatformIO python has Pillow)
"""
import json
import os
import re

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JSON_PATH = os.path.join(ROOT, "assets", "ferret-sprite-sheet.json")
PNG_PATH = os.path.join(ROOT, "assets", "ferret-sprite-sheet.png")
OUT = os.path.join(ROOT, "include", "accessory_sprites.h")

SCALE = 2.5
TRANSPARENT_KEY = 0xF81F
TAG_RE = re.compile(r"\(([^)]+)\)\s*(\d+)\.ase")

# ---------------------------------------------------------------- palette --
# Style rules extracted from ferret-sprite-sheet.png: a FULL 1px outline in
# the sheet's near-black rgb(47,47,46) around every shape, and two tones per
# material (like the fur's 105/130 pair). K = that outline color.
PAL = {
    ".": None,                 # transparent
    "K": (47, 47, 46),         # OUTLINE (the sheet's near-black)
    "D": (74, 48, 32),         # wood dark (bed frame shade)
    "W": (122, 82, 50),        # wood mid
    "M": (214, 198, 168),      # mattress cream
    "m": (181, 172, 148),      # mattress shade
    "P": (240, 234, 218),      # pillow white
    "V": (146, 54, 66),        # blanket wine
    "v": (112, 40, 50),        # blanket wine shade
    "C": (226, 208, 178),      # blanket cream stripe
    "R": (206, 44, 50),        # santa red
    "r": (160, 32, 40),        # santa red shade
    "S": (246, 246, 246),      # snow white
    "p": (238, 88, 124),       # party pink
    "b": (86, 168, 232),       # party blue
    "y": (248, 214, 92),       # party/star yellow
    "t": (226, 192, 100),      # straw
    "u": (196, 158, 72),       # straw shade
    "n": (122, 82, 44),        # straw band brown
    "A": (150, 198, 238),      # nightcap light blue
    "a": (108, 156, 205),      # nightcap blue shade
    "X": (98, 54, 138),        # witch purple
    "x": (72, 38, 104),        # witch purple shade
    "O": (232, 140, 44),       # witch band orange
}

# ------------------------------------------------------------- pixel maps --
# Every sprite follows the sheet's rule: full K outline, two tones/material.
# Blanket: smaller now (covers the rear half of the body, not the whole
# ferret), wine with a cream stripe and a shaded hem.
BLANKET = """
.KKKKKKKKKKKKK.
KVVVVVVVVVVVVVK
KCCCCCCCCCCCCCK
KVVVVVVVVVVVVVK
KvvvvvvvvvvvvvK
.KKKKKKKKKKKKK.
"""

# Nightcap: floppy LIGHT BLUE cone flopping right, white brim + pompom.
HAT_SLEEP = """
.........KKK
........KSSK
.....KKKAKK.
....KAAAK...
...KAAaK....
..KAAaK.....
.KSSSSSK....
.KKKKKKK....
"""

# Party hat: striped cone + yellow pompom.
HAT_PARTY = """
....KK...
...KyyK..
...KppK..
..KbbbbK.
..KppppK.
.KbbbbbbK
.KKKKKKKK
"""

# Santa: red cone bent right, white brim + white pompom.
HAT_SANTA = """
........KK.
.......KSSK
....KKKRSSK
...KRRRRKK.
..KRRRrK...
.KRRRrK....
KSSSSSSK...
KKKKKKKK...
"""

# Straw hat: short crown, brown band, wide brim.
HAT_PALHA = """
.....KKKKK.....
....KttttuK....
....KnnnnnK....
KKKKKttttuKKKKK
KtttuuuuuuutttK
.KKKKKKKKKKKKK.
"""

# Witch hat: crooked purple cone, orange band, wide brim.
HAT_BRUXA = """
.........KK....
........KXK....
.......KXXK....
......KXXK.....
.....KXXXK.....
....KXXXxK.....
....KOOOOK.....
KKKKXXXXXxKKKK.
.KKKKKKKKKKKKK.
"""

SPRITES = {
    "acc_blanket": (BLANKET, False),
    "hat_sleep": (HAT_SLEEP, True),   # True = also emit a mirrored _l variant
    "hat_party": (HAT_PARTY, True),
    "hat_santa": (HAT_SANTA, True),
    "hat_palha": (HAT_PALHA, True),
    "hat_bruxa": (HAT_BRUXA, True),
}

# Anchors: same export list/order as aseprite_to_frames.py ANIM_EXPORT.
ANIMS = [
    ("Idle", "idle", False), ("Idle", "idle_l", True),
    ("Idle2", "idle2", False), ("Idle2", "idle2_l", True),
    ("Movement", "walk", False), ("Movement", "walk_l", True),
    ("Jump", "jump", False), ("Jump", "jump_l", True),
    ("Dig", "dig", False),
    ("Sleep", "sleep", False),
]


def map_to_grid(art):
    rows = [r for r in art.strip("\n").split("\n")]
    w = max(len(r) for r in rows)
    return [r.ljust(w, ".") for r in rows], w, len(rows)


def rgb565(rgb):
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def emit_sprite(name, art, mirror, out):
    grid, w, h = map_to_grid(art)
    variants = [(name, grid)]
    if mirror:
        variants.append((name + "_l", [r[::-1] for r in grid]))
    lines = []
    for vname, vgrid in variants:
        img = Image.new("RGBA", (w, h))
        for y, row in enumerate(vgrid):
            for x, ch in enumerate(row):
                c = PAL[ch]
                img.putpixel((x, y), (*c, 255) if c else (0, 0, 0, 0))
        sw, sh = round(w * SCALE), round(h * SCALE)
        img = img.resize((sw, sh), Image.NEAREST)
        vals = []
        for y in range(sh):
            for x in range(sw):
                r, g, b, a = img.getpixel((x, y))
                vals.append(TRANSPARENT_KEY if a < 128 else rgb565((r, g, b)))
        lines.append(f"const uint16_t {vname}_w = {sw}, {vname}_h = {sh};")
        body = ",".join(f"0x{v:04X}" for v in vals)
        lines.append(f"const uint16_t {vname}[] PROGMEM = {{{body}}};")
        out.append((vname, sw, sh))
    return lines


def scan_anchors():
    """Pixel-exact head anchors from the EARS: rgb(249,137,112) only exists
    on the inner ears and the nose. Per frame: take the pink pixels within
    3px of the topmost pink row (= the ears; the nose sits lower), head
    center = midpoint of that cluster, skull top = topmost opaque pixel in
    those columns. Deterministic - no eyeballing."""
    PINK = (249, 137, 112)
    meta = json.load(open(JSON_PATH))
    frames = {}
    for fname, fr in meta["frames"].items():
        m = TAG_RE.search(fname)
        if m:
            frames.setdefault(m.group(1), []).append((int(m.group(2)), fr["frame"]))
    sheet = Image.open(PNG_PATH).convert("RGBA")

    anchors = {}
    for tag, ident, mirrored in ANIMS:
        pts = []
        for _, fr in sorted(frames[tag]):
            img = sheet.crop((fr["x"], fr["y"], fr["x"] + fr["w"], fr["y"] + fr["h"]))
            w, h = img.size
            pink = [(x, y) for x in range(w) for y in range(h)
                    if img.getpixel((x, y))[3] >= 128
                    and img.getpixel((x, y))[:3] == PINK]
            if not pink:
                pts.append(pts[-1] if pts else (w // 2, h // 2))
                continue
            top_pink = min(y for _, y in pink)
            ears = [(x, y) for x, y in pink if y <= top_pink + 3]
            ex0, ex1 = min(x for x, _ in ears), max(x for x, _ in ears)
            ax = (ex0 + ex1) // 2
            ay = None
            for y in range(h):
                if any(img.getpixel((x, y))[3] >= 128
                       for x in range(max(0, ex0 - 1), min(w, ex1 + 2))):
                    ay = y
                    break
            if mirrored:
                ax = w - 1 - ax
            pts.append((ax, ay if ay is not None else top_pink))
        anchors[ident] = pts
    return anchors


def main():
    body = ["// GENERATED by assets/accessory_sprites.py - do NOT edit by hand.",
            "// Accessory pixel art (32px grid @2.5x, ferret style) + per-frame",
            "// head-top anchors scanned from ferret-sprite-sheet.png.",
            "#pragma once", "#include <pgmspace.h>", ""]
    emitted = []
    for name, (art, mirror) in SPRITES.items():
        body.extend(emit_sprite(name, art, mirror, emitted))
    body.append("")

    anchors = scan_anchors()
    for ident, pts in anchors.items():
        flat = ",".join(f"{{{x},{y}}}" for x, y in pts)
        body.append(f"const int8_t acc_anchor_{ident}[][2] = {{{flat}}};")
        body.append(f"const uint8_t acc_anchor_{ident}_n = {len(pts)};")
    body.append("")
    body.append("// Head-top anchor (1x sprite coords, scale by 2.5 at draw time).")
    body.append("static inline bool accessoryAnchor(const char* anim, bool faceLeft,")
    body.append("                                   uint8_t frame, int* ax, int* ay) {")
    for ident in anchors:
        base = ident[:-2] if ident.endswith("_l") else ident
        cond = f'!strcmp(anim, "{base}")'
        if ident.endswith("_l"):
            cond += " && faceLeft"
        elif (ident + "_l") in anchors:
            cond += " && !faceLeft"
        body.append(f"  if ({cond}) {{")
        body.append(f"    const uint8_t i = frame < acc_anchor_{ident}_n ? frame : 0;")
        body.append(f"    *ax = acc_anchor_{ident}[i][0]; *ay = acc_anchor_{ident}[i][1];")
        body.append("    return true;")
        body.append("  }")
    body.append("  return false;")
    body.append("}")

    open(OUT, "w").write("\n".join(body) + "\n")
    total = sum(w * h * 2 for _, w, h in emitted)
    print(f"OK: {OUT} ({len(emitted)} sprites, {total} bytes PROGMEM)")
    for ident, pts in anchors.items():
        print(f"  anchors {ident}: {pts[:4]}{'...' if len(pts) > 4 else ''}")


if __name__ == "__main__":
    main()
