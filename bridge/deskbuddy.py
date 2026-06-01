#!/usr/bin/env python3
"""deskbuddy — Mac-side BLE bridge for the Claude Code desk-buddy.

One file, two roles (chosen by the first argument):

  deskbuddy daemon            run the persistent bridge (holds the BLE link,
                              listens on a local socket)
  deskbuddy set-state <s>     set the face expression directly
  deskbuddy event <e>         send a Claude Code event; the daemon maps it
                              to a state (and handles done/error auto-revert)
  deskbuddy status            print whether the daemon is up / board connected

The CLI half is a thin client: it forwards one line over a Unix socket and
exits immediately. If the daemon isn't running or the board is offline it
fails *silently* (exit 0) so it can never block or slow down Claude Code.

The daemon keeps the BLE connection open so per-hook calls don't pay the
1–3 s BLE connect latency. It reconnects automatically if the board drops.

Requires: bleak  (pip install bleak)
"""

import asyncio
import os
import socket
import sys
import time

# ─── Protocol / config ──────────────────────────────────────────────────────
DEVICE_NAME = "Claude Controller"
STATE_CHAR = "4c41555a-4465-7669-6365-000000000005"

STATES = ("idle", "thinking", "working", "waiting", "speaking", "done", "error")

# Claude Code event → face state. Mapping lives here so hooks stay dumb.
# Keys are the arg passed by the hook command (see docs/HOOKS_SPEC + the
# generated ~/.claude/settings.json). Verified against Claude Code v2.1.159.
EVENT_MAP = {
    "session-start":      "idle",      # SessionStart
    "prompt-submit":      "thinking",  # UserPromptSubmit
    "pre-tool":           "working",   # PreToolUse
    "post-tool":          "working",   # PostToolUse
    "permission-request": "waiting",   # PermissionRequest (needs user OK)
    "notification":       "waiting",   # Notification (fallback)
    "stop":               "done",      # Stop (turn finished)
    "subagent-stop":      "done",      # SubagentStop
    "tool-failure":       "error",     # PostToolUseFailure
    "stop-failure":       "error",     # StopFailure (API error)
    "error":              "error",
    "idle":               "idle",
}

# done / error are transient — fall back to idle after this many seconds.
REVERT_SECONDS = 5.0

RUN_DIR = os.path.expanduser("~/.deskbuddy")
SOCK_PATH = os.path.join(RUN_DIR, "sock")
LOG_PATH = os.path.join(RUN_DIR, "deskbuddy.log")


def _ensure_run_dir():
    os.makedirs(RUN_DIR, exist_ok=True)


# ─── CLI client half ─────────────────────────────────────────────────────────
def cli_send(line: str) -> None:
    """Forward one command line to the daemon, then exit. Never raises."""
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(0.4)
        s.connect(SOCK_PATH)
        s.sendall((line + "\n").encode())
        s.close()
    except Exception:
        pass  # daemon down / board offline → silent, never block Claude Code


def cli_status() -> None:
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(0.8)
        s.connect(SOCK_PATH)
        s.sendall(b"status\n")
        print(s.recv(256).decode().strip() or "daemon: up")
        s.close()
    except Exception:
        print("daemon: not running")


# ─── Daemon half ─────────────────────────────────────────────────────────────
class Bridge:
    def __init__(self):
        self.desired = "idle"          # state we want the board to show
        self.connected = False
        self.change = asyncio.Event()  # wakes the BLE writer
        self.revert_task = None

    def log(self, msg: str) -> None:
        line = f"{time.strftime('%H:%M:%S')} {msg}"
        print(line, flush=True)
        try:
            with open(LOG_PATH, "a") as f:
                f.write(line + "\n")
        except Exception:
            pass

    def apply_state(self, state: str) -> None:
        if state not in STATES:
            self.log(f"ignore unknown state: {state!r}")
            return
        self.desired = state
        self.change.set()
        # Cancel any pending auto-revert; schedule a fresh one for transients.
        if self.revert_task:
            self.revert_task.cancel()
            self.revert_task = None
        if state in ("done", "error"):
            self.revert_task = asyncio.create_task(self._revert_later())

    async def _revert_later(self):
        try:
            await asyncio.sleep(REVERT_SECONDS)
        except asyncio.CancelledError:
            return
        self.desired = "idle"
        self.change.set()
        self.log("auto-revert -> idle")

    def handle_command(self, line: str) -> None:
        parts = line.split()
        if not parts:
            return
        verb = parts[0]
        arg = parts[1] if len(parts) > 1 else ""
        if verb == "state":
            self.apply_state(arg)
        elif verb == "event":
            mapped = EVENT_MAP.get(arg)
            if mapped:
                self.apply_state(mapped)
            else:
                self.log(f"ignore unknown event: {arg!r}")

    # --- socket server ---
    async def serve(self):
        async def on_client(reader, writer):
            try:
                data = await asyncio.wait_for(reader.readline(), timeout=1.0)
                line = data.decode(errors="replace").strip()
                if line == "status":
                    msg = f"daemon: up  board: {'connected' if self.connected else 'offline'}  state: {self.desired}"
                    writer.write(msg.encode())
                    await writer.drain()
                else:
                    self.handle_command(line)
            except Exception:
                pass
            finally:
                writer.close()

        if os.path.exists(SOCK_PATH):
            os.unlink(SOCK_PATH)
        server = await asyncio.start_unix_server(on_client, path=SOCK_PATH)
        self.log(f"listening on {SOCK_PATH}")
        async with server:
            await server.serve_forever()

    # --- BLE link manager ---
    async def ble_loop(self):
        from bleak import BleakScanner, BleakClient
        while True:
            try:
                self.log(f"scanning for '{DEVICE_NAME}'...")
                dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=12)
                if not dev:
                    await asyncio.sleep(3)
                    continue
                self.log(f"connecting to {dev.address}")
                async with BleakClient(dev) as client:
                    self.connected = True
                    self.log("connected")
                    last = None
                    # Push current state immediately on (re)connect.
                    self.change.set()
                    while client.is_connected:
                        if self.desired != last:
                            try:
                                await client.write_gatt_char(
                                    STATE_CHAR, self.desired.encode(), response=False)
                                last = self.desired
                                self.log(f"-> {self.desired}")
                            except Exception as e:
                                self.log(f"write failed: {e}")
                                break
                        self.change.clear()
                        try:
                            await asyncio.wait_for(self.change.wait(), timeout=10)
                        except asyncio.TimeoutError:
                            pass
            except Exception as e:
                self.log(f"ble error: {e}")
            finally:
                self.connected = False
            await asyncio.sleep(2)  # reconnect backoff

    async def run(self):
        self.log("deskbuddy daemon starting")
        await asyncio.gather(self.serve(), self.ble_loop())


def run_daemon():
    _ensure_run_dir()
    try:
        asyncio.run(Bridge().run())
    except KeyboardInterrupt:
        pass


# ─── Entry point ─────────────────────────────────────────────────────────────
def main():
    _ensure_run_dir()
    argv = sys.argv[1:]
    if not argv:
        print(__doc__)
        return
    cmd = argv[0]
    if cmd == "daemon":
        run_daemon()
    elif cmd == "set-state" and len(argv) >= 2:
        cli_send(f"state {argv[1]}")
    elif cmd == "event" and len(argv) >= 2:
        cli_send(f"event {argv[1]}")
    elif cmd == "status":
        cli_status()
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()
