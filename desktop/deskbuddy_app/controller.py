"""AppController — the single hub: engine + BLE + hook socket + mode arbitration.

Owns one FaceEngine, one BleLink, one HookSocketServer, the asyncio loop, and
the auto/manual mode. Every state request carries a `source`; arbitration
decides whether hook events or manual UI clicks win (see the plan §Auto/Manual).

Threading: the asyncio-loop thread runs the engine/ble/socket. GUI-thread API
calls reach here only as coroutines via run_coroutine_threadsafe (see api.py),
so engine scheduling always happens on the loop thread.
"""

import asyncio
import json
import os
import time
from collections import deque

from .engine import FaceEngine, EVENT_MAP, DEVICE_NAME, USAGE_POLL_SECONDS, priority
from .ble import BleLink
from .socket_server import HookSocketServer
from . import usage

RUN_DIR = os.path.expanduser("~/.deskbuddy")
PREF_PATH = os.path.join(RUN_DIR, "app.json")
MANUAL_OVERRIDE_SECONDS = 8.0          # after a manual click, hold off equal/lower hooks


def _load_pref(key, default):
    try:
        with open(PREF_PATH) as f:
            return json.load(f).get(key, default)
    except Exception:
        return default


def _save_pref(key, value):
    data = {}
    try:
        with open(PREF_PATH) as f:
            data = json.load(f)
    except Exception:
        data = {}
    data[key] = value
    try:
        os.makedirs(RUN_DIR, exist_ok=True)
        with open(PREF_PATH, "w") as f:
            json.dump(data, f)
    except Exception:
        pass


class AppController:
    def __init__(self, loop):
        self.loop = loop
        self.mode = _load_pref("mode", "auto")        # "auto" | "manual"
        self.manual_override_until = 0.0
        self.engine = FaceEngine(self._on_engine_change, self.log)
        self.ble = BleLink(self.log, self._on_connection_change)
        self.sock = HookSocketServer(self.handle_hook_line, self.status_line, self.log)
        self.ui = None
        self.tray = None
        self.last_usage = None
        self._frame_cache = {}                         # state -> [png bytes] for tray animation
        self._usage_event = asyncio.Event()           # fires to force an immediate refetch
        self._log_ring = deque(maxlen=200)

    # ─── lifecycle ────────────────────────────────────────────────────────────
    async def start(self):
        self.log("clawdmeter console starting")
        await self.sock.start()
        self.loop.create_task(self.ble.run())
        self.loop.create_task(self._usage_loop())

    def attach_ui(self, ui):
        self.ui = ui

    def attach_tray(self, tray):
        self.tray = tray

    def resync_ui(self):
        """Push everything we know into the UI (called once the page is loaded)."""
        if not self.ui:
            return
        self.ui.on_mode(self.mode)
        self.ui.on_connection_change(self.ble.connected)
        self.ui.on_state_change(self.engine.desired, "init")
        if self.last_usage is not None:
            self.ui.on_usage(self.last_usage)
        for line in list(self._log_ring):
            self.ui.on_log(line)
        # First face snapshot: snapshot_face reads a value back from evaluate_js,
        # which must NOT run on the GUI thread (resync_ui is called from the page
        # `loaded` event). Hop onto the loop thread, where evaluate_js is safe.
        self.loop.call_soon_threadsafe(self._update_tray, self.engine.desired)

    # ─── logging (mirrors to UI log strip) ────────────────────────────────────
    def log(self, msg: str):
        line = f"{time.strftime('%H:%M:%S')} {msg}"
        self._log_ring.append(line)
        print(line, flush=True)
        if self.ui:
            self.ui.on_log(line)

    # ─── engine / ble callbacks ───────────────────────────────────────────────
    def _on_engine_change(self, state, source):
        self.ble.push(state)
        if self.ui:
            self.ui.on_state_change(state, source)
        self._update_tray(state)

    def _on_connection_change(self, connected):
        if self.ui:
            self.ui.on_connection_change(connected)
        self._update_tray(self.engine.desired)

    # ─── usage polling (host-side, always on; device piggybacks when connected) ─
    async def _usage_loop(self):
        if not usage.available():
            self.log("usage: fetcher unavailable (no bridge import)")
            return
        while True:
            payload = await usage.fetch()
            self.last_usage = payload
            if payload is not None:
                self.log(f"usage -> s={payload['s']}% w={payload['w']}% ({payload['st']})")
            else:
                self.log("usage: fetch returned no data")
            if self.ui:
                self.ui.on_usage(payload)
            if self.tray:
                self.tray.set_usage(payload)
            await self.ble.write_usage(payload)
            self._usage_event.clear()
            try:                                       # sleep, but wake early on refresh
                await asyncio.wait_for(self._usage_event.wait(), timeout=USAGE_POLL_SECONDS)
            except asyncio.TimeoutError:
                pass

    async def refresh_usage(self):
        self._usage_event.set()                        # nudge the loop to refetch now

    # ─── tray (menu-bar face) ─────────────────────────────────────────────────
    def _update_tray(self, state):
        if not self.tray:
            return
        png = self.ui.snapshot_face(state) if self.ui else None
        self.tray.set_state(state, png, self.ble.connected)
        self.tray.set_usage(self.last_usage)
        self.loop.create_task(self._render_tray_frames(state))

    async def _render_tray_frames(self, state):
        """Render (and cache) one animation loop of `state` for the tray face."""
        if not (self.tray and self.ui):
            return
        frames = self._frame_cache.get(state)
        if frames is None:
            frames = self.ui.snapshot_frames(state)   # one evaluate_js round-trip
            if frames:
                self._frame_cache[state] = frames
        if frames and state == self.engine.desired:
            self.tray.set_frames(state, frames)

    # ─── inbound from hooks (socket, on loop thread) ──────────────────────────
    def handle_hook_line(self, line: str):
        parts = line.split()
        verb = parts[0] if parts else ""
        arg = parts[1] if len(parts) > 1 else ""
        if verb == "event":
            self._inbound_event(arg)
        elif verb == "state":
            self._inbound_state(arg)

    def _inbound_event(self, name: str):
        if self.mode == "manual":
            self.log(f"manual mode: hook {name} ignored")
            return
        if self._override_holds_against(EVENT_MAP.get(name) if name != "prompt-submit" else "thinking"):
            self.log(f"manual override holds: drop {name}")
            return
        self.engine.handle_event(name, source="hook")

    def _inbound_state(self, state: str):
        # `deskbuddy set-state` from the CLI behaves like a hook (respects mode).
        if self.mode == "manual":
            self.log(f"manual mode: hook state {state} ignored")
            return
        if self._override_holds_against(state):
            self.log(f"manual override holds: drop {state}")
            return
        self.engine.apply_state(state, source="hook")

    def _override_holds_against(self, incoming_state) -> bool:
        if not incoming_state:
            return False
        now = time.monotonic()
        if now >= self.manual_override_until:
            return False
        return priority(incoming_state) <= priority(self.engine.desired)

    # ─── inbound from GUI (coroutines, scheduled by api.py) ────────────────────
    async def manual_set_state(self, state: str):
        if self.mode == "auto":
            self.manual_override_until = time.monotonic() + MANUAL_OVERRIDE_SECONDS
        ok = self.engine.apply_state(state, source="manual")
        return {"ok": ok, "state": self.engine.desired}

    async def set_mode(self, mode: str):
        if mode in ("auto", "manual"):
            self.mode = mode
            if mode == "manual":
                self.manual_override_until = 0.0
            _save_pref("mode", mode)
            self.log(f"mode -> {mode}")
            if self.ui:
                self.ui.on_mode(mode)
        return self.mode

    async def ble_connect(self):
        await self.ble.connect()
        return True

    async def ble_disconnect(self):
        await self.ble.disconnect()
        return True

    # ─── status (read-only; safe from any thread) ─────────────────────────────
    def status(self):
        return {
            "connected": self.ble.connected,
            "state": self.engine.desired,
            "mode": self.mode,
            "device_name": DEVICE_NAME,
        }

    def status_line(self):
        board = "connected" if self.ble.connected else "offline"
        return f"daemon: up  board: {board}  state: {self.engine.desired}"
