#!/usr/bin/env bash
# Local (UNSIGNED) build of ClawdSticker.app — for quick verification only.
#
# Produces desktop/dist/ClawdSticker.app. Because it is neither signed nor
# notarized, Gatekeeper flags it on first launch: right-click → Open (once).
# Signing + notarization + .dmg happen only in CI (.github/workflows/release-macos.yml).
#
# Usage:  desktop/packaging/build.sh
set -euo pipefail

DESKTOP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DESKTOP_DIR"

VENV="${VENV:-.venv}"
if [ ! -d "$VENV" ]; then
  echo "Creating venv at $VENV ..."
  python3 -m venv "$VENV"
fi

"$VENV/bin/pip" install --quiet --upgrade pip
"$VENV/bin/pip" install --quiet -r requirements.txt pyinstaller

CLAWDSTICKER_VERSION="${CLAWDSTICKER_VERSION:-0.0.0-dev}" \
  "$VENV/bin/pyinstaller" packaging/ClawdSticker.spec --noconfirm --clean

echo ""
echo "✅ Built: $DESKTOP_DIR/dist/ClawdSticker.app"
echo "   open 'dist/ClawdSticker.app'      # first launch: right-click → Open"
