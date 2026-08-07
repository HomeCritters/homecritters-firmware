# To do

Open work, roughly in order of "we'd love someone to pick this up". Ideas and
PRs welcome — see [`CONTRIBUTING.md`](CONTRIBUTING.md). Comment on the matching
issue (or open one) before starting something big, so two people don't build
the same thing.

`CLAUDE.md` keeps the longer, messier engineering backlog; this file is the
part that's ready for someone else to grab.

---

## 🌍 Internationalisation (i18n) — help wanted

**Status:** not started. Greenfield, self-contained, great first big
contribution.

Today **every user-facing string is hardcoded in Portuguese (pt-BR)** — the
project owner is Brazilian and the device grew up speaking Portuguese. Code and
comments are already in English, so the split is clean; what's missing is a
translation layer.

The goal is to make the language switchable, with **English as the first
translation**, and to make adding a third language a matter of dropping in one
file.

### What it involves

**Firmware (`src/`, `include/`)**

- A string table — an enum of message IDs plus per-language arrays in
  `PROGMEM`, e.g. `include/i18n/strings_pt.h`, `strings_en.h`, and a
  `t(STR_FEED)` accessor. Flash is the constraint (the sprite and sound headers
  already eat several MB), so the table should hold *only* UI text, and adding
  a language shouldn't cost anything for users who don't select it — a
  compile-time default with the alternatives opt-in via `build_flags` is a
  perfectly good v1 if runtime switching proves too expensive.
- Replace the literals in `Renderer*.cpp`, the menus, the games and the screen
  table in `main.cpp`.
- Language chosen in the config menu (Main → a new tile, or under Conexão) and
  persisted in NVS, next to the other preferences in `Pet`/settings. Remember
  the NVS version migration.
- **Watch the round screen.** English strings are often longer than Portuguese
  ones; anything that grows will collide with the r≈119.5 bezel or overflow a
  menu tile. `drawScrollText` (marquee) exists for long labels. Every changed
  screen needs a `tools/hwshot.py` screenshot **with the bezel ring** before
  the PR — that is exactly what the ring is there to catch.
- Accented characters: check the font actually has the glyphs for whatever
  language is added (Portuguese already needs ã/ç/õ).

**Web portal (`web/src/`)**

- A matching layer — a small `t()` helper with a JSON dictionary per language
  is enough; no need for a heavy i18n framework, the bundle is inlined into
  flash and every kilobyte is real. Language should follow the device setting,
  with a manual override.
- Strings live across `App.jsx`, `components/*.jsx` and `options.js`.
- Rebuild and regenerate `include/web_index.h` in the same commit.

**Home Assistant integration** — the
[plugin repo](https://github.com/HomeCritters/homecritters-ha-plugin) already
ships EN entity names with a PT translation, so it's mostly done. Keep the
device and the plugin consistent if you rename anything.

### Suggested first slice

Don't try to land it all at once. A first PR that adds the mechanism and
translates **one screen** (the config menu is a good candidate — lots of short
labels, easy to screenshot) is much easier to review than a big-bang change,
and settles the design before the tedious part.

---

## 🔊 Redistributable sound effects — help wanted

The clips in `include/sounds/` came from public sound-effect sites during
development and **their provenance was never cleared**
([details](THIRD_PARTY_NOTICES.md#sound-effects)). Replacing them with
CC0/public-domain equivalents (Freesound, Pixabay, or clips you made yourself)
would let the whole repository be redistributed cleanly.

Can be done piecemeal — one sound per PR is fine. Keep the mixable SFX as
16 kHz mono WAV (`assets/mixable_wavs.sh`).

---

## 🦦 Sprite licensing — maintainer

Leon's sprite sheet is [Elthen's](THIRD_PARTY_NOTICES.md#artwork-leon-the-ferret),
under a non-commercial license that appears to rule out redistributing the
asset files — which is what this repository does. Needs resolving by the
maintainer, not by a PR: ask Elthen for explicit permission to ship the frames
in an open-source firmware repo, buy a grant that covers it, ship a fetch
script instead of the pixels, or commission replacement art.

---

## Smaller things

- **Offline time.** Stat decay only counts time the device was powered on.
  `Clock` already syncs NTP, so saving the timestamp on shutdown and computing
  the gap on boot would fix it.
- **Battery curve.** `Battery::percent()` is a rough approximation; worth
  calibrating against real resting voltage.
- **The Death animation** from the sprite sheet is exported but never used —
  e.g. for a stat left at zero for too long.
- **OTA updates**, so a reflash doesn't need USB.
- **Coins / a little shop** — spend what you earn in the mini-games.
- **Remote access** from outside the house. Sketched plan: Tailscale on a
  helper device plus Funnel, which would mean moving the WebSocket to port 80
  behind an HTTP Upgrade.
- **Sendspin** (the OHF's open synchronised multi-room audio protocol, served
  by Music Assistant). Would bring album art on screen and LED/dance-floor
  effects on the *real* beat, plus multi-room sync. Not now — it's experimental
  and an esp-idf component, so a heavy port to Arduino. Revisit when the
  protocol stabilises.
