# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

# Project context

ESP32-S3 firmware for a desk-side **Claude Code companion** — a small device whose
screen shows a cartoon face whose expression follows Claude Code's real-time
state (idle / thinking / working / waiting / done / error / speaking) and that
chirps a short tone on the key states. **Fork of `HermannBjorgvin/Clawdmeter`**;
the upstream project displays Anthropic API usage numbers, which we're
replacing wholesale with the expression system. The HAL + per-board driver
layer + NimBLE peripheral plumbing are reused as-is.

**Read these first** before touching code:

- @docs/PROJECT_BRIEF.md — overall goals, hardware lock, architecture, MVP scope, acceptance
- @docs/UI_SPEC.md — 7-state expression animation spec (LVGL vector primitives + `lv_anim`, no sprite sheets)
- @docs/HOOKS_SPEC.md — Claude Code event → state mapping; Mac bridge CLI design
- @docs/UI_PREVIEW.html — visual reference prototype; **animation behavior is canonical here**
- @docs/ui_preview_states/ — captured PNGs of all 7 states at native 368×448, plus a full-page overview; use as LVGL implementation anchors
- @docs/porting/ — HAL contract & capability flags (still authoritative for board ports)

## Working agreements

These come from the user — not the codebase. Treat them as rules of engagement:

- **Reuse-first.** The HAL contract in `firmware/src/hal/`, the `boards/waveshare_amoled_18/` drivers, the NimBLE framework in `ble.{h,cpp}`, the PlatformIO env, and the font/screenshot tooling all carry over. Replacement work should slot *into* this framework, not around it.
- **Small verifiable steps.** Every change should compile and flash. Land a static face before animating; a static animation before state switching; one state before all seven.
- **Brief before build.** When given a new task, state your understanding + plan first; confirm with the user before writing code. This applies even when the task feels obvious — surprises usually live in the part that felt obvious.
- **Cross-check Claude Code hooks against current official docs.** Event names + payload variables shift between Claude Code versions. @docs/HOOKS_SPEC.md gives design intent; the names there must be reverified before you wire them into `~/.claude/settings.json`.

## Current phase: MVP (first step)

| Must do | Out of scope (later) |
|---|---|
| Reuse upstream NimBLE peripheral; replace JSON usage payload with **state string** | Dangerous-command warnings |
| 7 LVGL vector face animations (idle / thinking / working / waiting / speaking / done / error) | TTS / voice playback |
| `waiting` / `done` / `error` cue tones via ES8311 + onboard speaker | Microphone / voice input |
| Mac-side Python BLE-central **bridge** + `deskbuddy` CLI driven by Claude Code hooks | Dida365 / WiFi / multi-session / touch interactions |

**Hardware lock:** Waveshare ESP32-S3-Touch-AMOLED-1.8 only (`firmware/src/boards/waveshare_amoled_18/`). The upstream AMOLED-2.16 port still builds, but isn't a desk-buddy target.

**Why BLE over WiFi:** corporate / home WiFi often has AP isolation that drops device-to-device packets, and the user can't always reconfigure the router. BLE works without provisioning, reuses upstream Clawdmeter's NimBLE plumbing, and keeps the device plug-and-play in the only deployment context that matters (Mac sitting nearby, same room). WiFi is parked for "phone control / multi-device / cloud agent" futures.

## What stays, what gets replaced

**Reused as-is:**
- `firmware/src/hal/` — board-agnostic HAL contracts
- `firmware/src/boards/waveshare_amoled_18/` — SH8601 / FT3168 / AXP2101 / XCA9554 drivers
- `firmware/src/ble.{h,cpp}` — NimBLE peripheral framework (the GATT *schema* changes, the framework doesn't)
- `firmware/src/theme.h` palette structure — **but** add a new state-color set (`#cdd6f4 #7dd3fc #5b9dff #f5b53d #c08bf0 #46d68a #f06a5e`) for the 7 states; existing `THEME_ACCENT` doesn't cover them
- PlatformIO env, font tooling, `screenshot.sh` QA loop

**Will be replaced/retired by the expression system:**
- `firmware/src/ui.{h,cpp}` — 3-screen splash/usage/bluetooth layout → single face surface
- `firmware/src/splash.{h,cpp}` + `splash_animations.h` + claudepix tooling — sprite-sheet engine no longer needed
- `firmware/src/usage_rate.{h,cpp}` + `data.h::UsageData` + `icons.h` battery/usage icons — usage telemetry gone
- `firmware/src/idle.{h,cpp}` + `idle_cfg.h` — TBD: the face's `idle` state may subsume auto-sleep, or sleep stays as a wrapper around it
- `daemon/` (Bash + Python Anthropic API pollers) — superseded by the Mac-side bridge in `docs/HOOKS_SPEC.md`

# Hardware (AMOLED-1.8 — locked target)

- Display: **SH8601** AMOLED via QSPI (CS=12, **SCLK=11**, SDIO0..3=4..7, RST routed via XCA9554 EXIO1). Fixed at 0°; no rotation.
- Touch: **FT3168** via I2C (SDA=15, SCL=14, INT=21, addr=0x38). Inline reader in `main.cpp` (FocalTech standard register layout — avoids vendoring the GPLv3 `Arduino_DriveBus` library).
- PMU: **AXP2101** @ I2C 0x34 (`XPowersLib`). Battery is an optional kit add-on; PMU + charging circuitry are populated.
- IMU: **QMI8658** @ I2C 0x6B. Initialized for I2C bus health, rotation logic disabled.
- IO expander: **XCA9554 / PCA9554** @ I2C 0x20. Gates LCD_RST, TP_RST, audio amp enable, and reads the PWR button (EXIO4, active HIGH).
- Audio: **ES8311** codec + onboard speaker + mic (mic unused in MVP).
- Buttons: GPIO 0 (BOOT — Space/voice-mode HID key) + XCA9554 EXIO4 (PWR — cycle screens / cycle animations on splash).

For the upstream AMOLED-2.16 board's pins (CO5300 + CST9220 + GPIO 18), see git history of this file or `boards/waveshare_amoled_216/board.h`.

# Architecture

```text
firmware/src/
  hal/                      — board-agnostic interfaces shared code calls into
    board_caps.h            — runtime BoardCaps struct (W, H, button_count, has_* flags)
    display_hal.h           — init / begin / set_brightness / draw_bitmap / tick / round_area
    touch_hal.h             — init / read(&x, &y, &pressed)
    input_hal.h             — init / is_held(PRIMARY|SECONDARY)
    power_hal.h             — init / tick / battery_pct / is_charging / pwr_pressed (edge)
    imu_hal.h               — init / tick / rotation_quadrant
  boards/
    waveshare_amoled_18/    — desk-buddy target. SH8601 + FT3168 + AXP + XCA9554, no rotation
    waveshare_amoled_216/   — upstream Clawdmeter board (still builds, not a desk-buddy target)
    template/               — copy this to bootstrap a new port
  main.cpp                  — setup() + loop(): HAL calls only, zero #ifdef BOARD_*
  ble.{h,cpp}               — NimBLE peripheral (will get a desk-buddy state characteristic)
  theme.h                   — design tokens (extend with state-color palette for the 7 expressions)
  ui.{h,cpp}, splash.*, usage_rate.*, idle.*, data.h, icons.h, splash_animations.h
                            — upstream UI stack; being replaced by the face system (see "What gets replaced" above)
  font_*.c                  — pre-compiled LVGL 9 bitmap fonts
docs/porting/               — adding-a-board.md, hal-contract.md, capability-flags.md
```

Each board folder contains: `board.h` (pins, I2C addresses, `BOARD_HAS_*` flags), `board_init.cpp` (Wire.begin + any IO expander), `display.cpp`, `touch.cpp`, `input.cpp`, `power.cpp`, `imu.cpp`, `caps.cpp` (the `BoardCaps` instance), plus any board-private hardware drivers (e.g. `io_expander.{h,cpp}` on AMOLED-1.8). PlatformIO's `build_src_filter` includes shared code + one board's folder per env.

# Build / flash

```bash
pio run -d firmware -e waveshare_amoled_18                                                # build (desk-buddy target)
pio run -d firmware -e waveshare_amoled_18 -t upload --upload-port /dev/cu.usbmodem101    # flash on macOS
pio run -d firmware -e waveshare_amoled_18 -t upload --upload-port /dev/ttyACM0           # flash on Linux
pio run -d firmware -e waveshare_amoled_216                                               # upstream board, still builds
```

Repo-root convenience wrappers (carry over from upstream Clawdmeter — they currently target the 2.16 env; expect updates as the desk-buddy bridge lands):

```bash
./flash.sh              # Linux upload (currently 2.16 env)
./flash-mac.sh [PORT]   # macOS upload, auto-detects /dev/cu.usbmodem*
./install.sh            # Linux: installs upstream systemd --user daemon
./install-mac.sh        # macOS: installs upstream LaunchAgent + Python venv
```

If `pio` isn't on PATH: try `~/.platformio/penv/bin/pio` or `brew install platformio` on macOS. Device path: `/dev/cu.usbmodem*` on macOS, `/dev/ttyACM0` on Linux — both expose the ESP32-S3 native USB-JTAG (no boot-mode dance needed).

# QA your own UI changes — don't ask the user

The firmware ships a `screenshot` serial command that dumps the LVGL framebuffer. `./screenshot.sh out.png [port]` captures a PNG sized to the active display (368×448 on AMOLED-1.8). **Use this on every UI iteration** — Read the PNG with the Read tool, verify the change visually, iterate. Script auto-picks the macOS/Linux default port and falls back to pio's bundled Python if pyserial isn't on the system Python.

For the face system specifically, cross-check against `docs/ui_preview_states/*.png` (rendered from `docs/UI_PREVIEW.html` at native 368×448). Color, glow intensity, eye/brow geometry, and accessory placement should match the prototype within a reasonable visual tolerance — exact animation frames don't have to.

If your UI work needs the device to *start* on a non-default screen for screenshot iteration, temporarily change the default boot state in `main.cpp` and revert before committing.

# Critical gotchas

These all still apply to the desk-buddy build (AMOLED-1.8). Items only relevant to the AMOLED-2.16 board (CO5300 rotation, CST9220 touch swap/mirror) are kept in git history of this file.

1. **OPI PSRAM** required: `board_build.arduino.memory_type = qio_opi` in `firmware/platformio.ini`. Without this, `MALLOC_CAP_SPIRAM` returns NULL and the screen is black. The face system's double-buffered LVGL canvas lives in PSRAM — even more critical here than for the upstream sprite engine.
2. **pioarduino platform required.** GFX Library for Arduino needs Arduino Core 3.x (`esp32-hal-periman.h`), not the 2.x that standard `espressif32` ships. We pin `pioarduino/platform-espressif32` 55.03.38-1.
3. **LVGL 9 font patching.** `lv_font_conv` outputs LVGL 8 format. Must remove `#if LVGL_VERSION_MAJOR >= 8` guards, drop `.cache` field, add `.release_glyph`, `.kerning`, `.static_bitmap`, `.fallback`, `.user_data`. Without patching, fonts render invisible.
4. **Per-board pre-init is `board_init()`.** `boards/waveshare_amoled_18/board_init.cpp` brings up `Wire` and **must release the XCA9554 IO expander** before `display_hal_init()` or `ft3168_init()` — otherwise SH8601 + FT3168 stay in reset and silently fail to probe.
5. **Touch reading is centralized inside each board's `touch.cpp`.** The HAL `touch_hal_read()` is called once per loop from `my_touch_cb`; the board's implementation owns its latched `touch_pressed/x/y` state. Don't call the underlying controller from anywhere else — concurrent callers consume each other's I2C transactions.
6. **Even-aligned flush regions.** `display_hal_round_area` (called from `rounder_cb`) enforces this. Harmless on SH8601 but required as a HAL invariant; don't drop it when refactoring the UI surface.
7. **LVGL RGB565A8 is planar** (only relevant if the face system grows alpha icons). `w*h` RGB565 pixels followed by `w*h` alpha bytes; `data_size = w*h*3`, `stride = w*2`. Use `init_icon_dsc_rgb565a8()` for icons overlapping non-uniform backgrounds. Lucide source PNGs are black-on-transparent — `tools/png_to_lvgl.js` must tint to white or icons render invisible.
8. **No `#ifdef BOARD_*` in shared code.** The whole point of the upstream refactor — if you're about to add one, you probably want a `BoardCaps` field or a per-board file instead. See `docs/porting/capability-flags.md`.
9. **Container-query units in `docs/UI_PREVIEW.html` → explicit pixels in LVGL.** The HTML uses `cqw / cqh` so the design scales to any screen; LVGL has no equivalent. Convert at implementation time: `1cqw = 3.68px`, `1cqh = 4.48px` on 368×448.

# Splash / claudepix pipeline (upstream — being retired)

The upstream firmware uses 13 × 20×20 pixel-art creature animations sourced from [claudepix.vercel.app](https://claudepix.vercel.app):

```bash
node tools/scrape_claudepix.js  # → tools/claudepix_data/*.json
node tools/convert_to_c.js      # → firmware/src/splash_animations.h
```

The desk-buddy face system replaces this. Keep the tooling around until the new system is verified, then it can be removed alongside `splash.{h,cpp}` and `splash_animations.h`.

# Daemon / host side (upstream — being replaced by Mac bridge)

The two upstream daemons (Linux Bash `daemon/claude-usage-daemon.sh` + systemd unit, macOS Python `daemon/claude_usage_daemon.py` + LaunchAgent) poll the Anthropic API and push `UsageData` JSON over BLE GATT char `4c41555a-...0002`. **The desk-buddy MVP replaces both with a Mac-side Python BLE-central bridge** driven by Claude Code hooks — design in `docs/HOOKS_SPEC.md`. The bridge writes a state string instead of usage JSON; the GATT service UUID family can be reused, but expect a new characteristic for the state stream.

Keep the upstream daemons functional until the bridge lands so the build doesn't bit-rot.

# User profile / preferences

See `~/.claude/projects/.../memory/` files for persistent context (user is an embedded-beginner senior dev, brand-conscious, prefers iterative UI refinement, dislikes me authoring my own art when third-party assets are intended). Always read those memory files at session start.

# Recent session highlights

- **Project pivot to desk-buddy (2026-05-23 → 24).** Fork's direction changed from Claude Code usage dashboard → Claude Code state-aware expression companion. Hardware locked to AMOLED-1.8. Top-level docs moved to `docs/` (PROJECT_BRIEF, UI_SPEC, HOOKS_SPEC, UI_PREVIEW.html + per-state screenshots). HAL / `boards/waveshare_amoled_18/` / NimBLE plumbing reused; `ui.cpp` / `splash.*` / `usage_rate.*` / `data.h` / upstream daemons slated for retirement once the new face system + Mac bridge land.
- **Device-abstraction refactor (2026-05-18).** All board-conditional code moved out of shared files into `boards/<name>/` and behind a HAL in `hal/`. ~30 `#ifdef BOARD_*` blocks went to zero. UI is responsive via `compute_layout()` driven by `board_caps()`. New ports add a folder + a PlatformIO env — no shared file edits.
- Added second board port: Waveshare AMOLED-1.8 (368×448 portrait, SH8601, FT3168, XCA9554 IO expander).
- Migrated from Panlee SC01 Plus (480×320 IPS) to Waveshare 2.16" AMOLED (480×480 square). Full hardware/library swap.
