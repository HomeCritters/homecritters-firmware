# Contributing to HomeCritters firmware

Thanks for wanting to help Leon out. 🦦

This is a hobby project built around one specific board, so the most useful
contributions are usually small and concrete: a bug fix, a new mini-game, a
weather effect, a festive theme, a translation, or a sound.

By participating you agree to follow our [Code of Conduct](CODE_OF_CONDUCT.md).

## Before you start

- **Open an issue first** for anything bigger than a bug fix. It avoids two
  people building the same thing, and some ideas need a hardware decision.
- Check [`CLAUDE.md`](CLAUDE.md) — it is the long-form architecture document
  (module responsibilities, protocol details, hardware gotchas). The README is
  the tour; `CLAUDE.md` is the map.
- The Home Assistant integration lives in a **separate repository**:
  [homecritters-ha-plugin](https://github.com/HomeCritters/homecritters-ha-plugin).
  Protocol changes usually need a PR in both.

## Development setup

You need [PlatformIO](https://platformio.org/) (the CLI is enough) and, if you
touch the web portal, Node 20+.

```bash
pio run                 # build the firmware
pio run -t upload       # flash over USB-C (hold BOOT if no port shows up)
pio device monitor      # serial log, 115200 baud
```

> `pio` is often outside `PATH`; use `~/.platformio/penv/bin/pio`.

### Web portal

The portal is a React (Vite + Ant Design) app built as a **single file** and
embedded gzipped in flash. After editing `web/src`:

```bash
cd web && npm install && npm run build && cd ..
python3 assets/web_to_header.py   # regenerates include/web_index.h
pio run -t upload
```

`web/dist/` is not versioned — only the generated `include/web_index.h` is.
**Commit the regenerated header** with your portal change, otherwise the
firmware still serves the old portal.

### Generated assets

Files marked `GENERATED ... do NOT edit` are exactly that. Regenerate them with
the scripts in `assets/`, never by hand:

```bash
python3 assets/aseprite_to_frames.py                       # include/ferret_anim.h
python3 assets/mp3_to_header.py <file.mp3> <symbol> include/sounds/<name>.h
python3 assets/web_to_header.py                            # include/web_index.h
```

## Testing your change

There is no unit-test suite — this is firmware for one board, and the real test
is the device. What we do ask:

1. **`pio run` must build clean.** CI checks this on every PR.
2. **Screenshot any visual change.** The firmware has a serial console and a
   web screenshot endpoint; `tools/hwshot.py` renders the framebuffer to PNG
   **with the round bezel ring drawn** — the physical screen is a circle and
   the canvas is a square, so anything outside the r≈119.5 ring is invisible on
   the real device. Corners of "square" layouts are the usual victims.

   ```bash
   PY=~/.platformio/penv/bin/python
   $PY tools/hwshot.py -o shot.png                  # current screen
   $PY tools/hwshot.py --cmd games -o games.png     # navigate, then capture
   ```

   Attach the PNG to the PR. See [`tools/README.md`](tools/README.md) for the
   caveat that opening the serial port resets the board (use the web
   `/shot.bmp` endpoint to capture live state like the NTP clock).
3. **Say what hardware you tested on.** "Built only, not flashed" is a fine and
   honest answer — just say so.

## Code style

- **Code and comments in English. UI strings (device screen and web portal) in
  Portuguese (pt-BR).** The project owner is Brazilian and the product speaks
  Portuguese; the codebase speaks English.

  > 🌍 We'd like to change the second half of that. There is **no i18n layer
  > yet** — building one, with English as the first translation, is an open
  > invitation and one of the most useful things anyone could contribute. See
  > [`TODO.md`](TODO.md#-internationalisation-i18n--help-wanted). Until it
  > exists, new strings go in Portuguese; please keep them in one place per
  > screen rather than scattered inline, so they're easy to extract later.
- Keep modules single-responsibility and unaware of each other (`Pet` = rules,
  `Renderer` = pixels, `InputController` = events). `main.cpp` is the only
  place that stitches things together.
- `include/pins.h` is the **single source of truth** for the pinout. Never
  hardcode a GPIO number in a module.
- Every new screen function in the `Renderer` must open with `beginScreen()`
  and **end with `endScreen()`** — that is the only place that pushes the
  sprite. Forget it and the LCD freezes on the previous frame while screenshots
  still look correct (they read the canvas, not the LCD).
- A new screen is **one row in `SCREENS[]` + one case in `enterScreen()`** in
  `main.cpp`. Dispatch, portal name, pacing and timeouts come from the table.
- A new long-running task must subscribe to the watchdog (`esp_task_wdt_add`)
  and feed it, and register itself with `taskreg::add` so it shows up in `top`.
- Match the surrounding style — comment density, naming, and idiom.

### Every new feature deserves a sound effect

Project tradition. Find something that fits (MyInstants is the usual source),
convert it with `assets/mp3_to_header.py` into `include/sounds/`, and include
it from `AudioPlayer.cpp`. Short SFX should be mixable (16 kHz mono WAV, see
`assets/mixable_wavs.sh`) so they can play over ambient audio.

**Only contribute audio you have the right to redistribute** under this
project's license — see [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
If you are unsure about a clip's provenance, say so in the PR instead of
quietly adding it.

## Commits and pull requests

- Small, focused commits with a descriptive message. Conventional-commit
  prefixes (`fix:`, `feat:`, `docs:`) are welcome but not required.
- One topic per PR. Fill in the PR template.
- Keep generated headers and their source in the same commit.
- Be patient — this is a spare-time project.

## Licensing of contributions

By submitting a pull request you agree that your contribution is licensed under
the [MIT License](LICENSE), the same as the rest of the project. There is no
CLA.
