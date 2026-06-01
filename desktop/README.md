# Clawdmeter Console (desktop)

A macOS desktop app that controls the ESP32-S3 desk-buddy over BLE and is the
single owner of the Claude Code hook link. Python + pywebview; the web UI reuses
the clawpet face renderer as a live mirror.

```
┌─────────────────────────┐
│ pywebview window (web/)  │  20-state grid · live face · log · mode toggle
├─────────────────────────┤
│ AppController (loop thr) │  auto/manual arbitration
│  ├ FaceEngine            │  state vocab + priority/transient/danger-sticky
│  ├ BleLink (bleak)       │  writes STATE_CHAR to "Claude Controller"
│  └ HookSocketServer      │  ~/.deskbuddy/sock  ← Claude Code hooks
└─────────────────────────┘
```

It **replaces** the old `bridge/deskbuddy.py daemon` (only one process may hold
the BLE link). Existing hooks keep working unchanged — they still call
`bridge/deskbuddy event <name>`, which writes one line to `~/.deskbuddy/sock`;
this app owns that socket now.

## Run (dev)

```bash
python3 -m venv desktop/.venv
desktop/.venv/bin/pip install -r desktop/requirements.txt
# the old daemon must NOT be running (one BLE owner):
launchctl bootout gui/$(id -u)/com.deskbuddy.bridge 2>/dev/null
PYTHONPATH=desktop desktop/.venv/bin/python -m deskbuddy_app
```

Close the window → it hides; the asyncio thread (BLE + hooks) keeps running.
The menu-bar 🐾 re-shows the window or quits.

## Modes

- **跟随 Claude Code (auto)** — hook events drive the face. A manual click wins
  for `MANUAL_OVERRIDE_SECONDS` (8s) over equal/lower-priority hooks.
- **手动 (manual)** — hooks are ignored (silently, never blocking Claude Code);
  only the grid drives the face.

## Layout

- `deskbuddy_app/engine.py` — state vocabulary + arbitration (single source).
- `deskbuddy_app/ble.py` — bleak link + STATE_CHAR write + usage poll.
- `deskbuddy_app/controller.py` — hub + auto/manual mode.
- `deskbuddy_app/socket_server.py` — `~/.deskbuddy/sock` (hooks/CLI endpoint).
- `deskbuddy_app/{api,bridge_to_ui,app}.py` — JS↔Python bridge, pushes, bootstrap+tray.
- `web/` — `index.html`, `css/console.css`, `js/{moods,face,console,bridge}.js`.
  `web/js/moods.js` mirrors firmware `face.cpp` (20 canonical states + colors).

## Not yet (roadmap)

Device settings (brightness/flip/idle) · WiFi provisioning · AI config — each is
a future panel + new firmware GATT characteristic. See the plan in
`~/.claude/plans/` and CLAUDE.md.
