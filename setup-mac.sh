#!/usr/bin/env bash
# ClawdSticker — one-shot host setup for macOS.
#
# Wires up everything on the Mac side in one command:
#   1. a Python venv for the `deskbuddy` CLI that Claude Code hooks call
#   2. merges ClawdSticker's hooks into ~/.claude/settings.json (idempotent, backed up)
#   3. optionally flashes the firmware (if PlatformIO + a board are present)
#
# It does NOT install a login autostart agent — open ClawdSticker.app yourself.
# Re-runnable: safe to run again after pulling updates.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
echo "=== ClawdSticker setup (macOS) ==="
echo "repo: $REPO"
echo ""

command -v python3 >/dev/null || { echo "Error: python3 is required."; exit 1; }

# 1) venv for the deskbuddy CLI ------------------------------------------------
# The CLI only forwards hook events over a local socket (pure stdlib), so the
# venv just needs to exist for the bridge/deskbuddy wrapper. bleak is only
# needed for the optional standalone `deskbuddy daemon` (ClawdSticker.app owns
# the BLE link in normal use), so we don't install it here.
echo "[1/3] Python venv for the deskbuddy CLI"
if [ ! -d "$REPO/bridge/.venv" ]; then
  python3 -m venv "$REPO/bridge/.venv"
  echo "  created bridge/.venv"
else
  echo "  bridge/.venv already present"
fi
chmod +x "$REPO/bridge/deskbuddy"
echo ""

# 2) Claude Code hooks ---------------------------------------------------------
echo "[2/3] Merging Claude Code hooks into ~/.claude/settings.json"
python3 "$REPO/bridge/install_hooks.py" "$REPO/bridge/deskbuddy"
echo ""

# 3) firmware (optional) -------------------------------------------------------
echo "[3/3] Firmware"
PIO="$(command -v pio || true)"
[ -z "$PIO" ] && [ -x "$HOME/.platformio/penv/bin/pio" ] && PIO="$HOME/.platformio/penv/bin/pio"
PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
if [ -n "$PIO" ] && [ -n "$PORT" ]; then
  read -r -p "  Flash firmware to $PORT now? [y/N] " ans
  if [[ "${ans:-}" =~ ^[Yy]$ ]]; then
    "$PIO" run -d "$REPO/firmware" -e waveshare_amoled_18 -t upload --upload-port "$PORT"
  else
    echo "  skipped."
  fi
else
  echo "  PlatformIO or a connected board not found — flash manually:"
  echo "    pio run -d firmware -e waveshare_amoled_18 -t upload --upload-port /dev/cu.usbmodem101"
fi
echo ""

# next steps -------------------------------------------------------------------
echo "=== Done. Next steps ==="
if [ -d "/Applications/ClawdSticker.app" ]; then
  echo "  ✓ ClawdSticker.app is installed."
else
  echo "  • Download ClawdSticker .dmg from the Releases page, drag it to Applications, then:"
  echo "      xattr -dr com.apple.quarantine \"/Applications/ClawdSticker.app\""
fi
echo "  • Open ClawdSticker (look for 🐾 in the menu bar) and grant the Bluetooth permission."
echo "  • Power on the device, then use Claude Code anywhere — the face follows along."
