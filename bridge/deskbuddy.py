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
import getpass
import json
import os
import re
import socket
import subprocess
import sys
import time
import urllib.request
import urllib.error
from pathlib import Path

# ─── Protocol / config ──────────────────────────────────────────────────────
DEVICE_NAME = "Claude Controller"
STATE_CHAR = "4c41555a-4465-7669-6365-000000000005"  # face-state string
RX_CHAR    = "4c41555a-4465-7669-6365-000000000002"  # usage JSON (original UI)

# ─── Usage polling (original Claude-usage UI) ─────────────────────────────────
# The device's usage screen wants {s,sr,w,wr,st,ok}. The only way to read the
# 5h/7d utilization is the rate-limit response headers, so we make a tiny API
# call (max_tokens:1) with the Claude Code OAuth token, every USAGE_POLL_SECONDS.
USAGE_POLL_SECONDS = 300
KEYCHAIN_SERVICE = "Claude Code-credentials"          # macOS Keychain item
CREDENTIALS_PATH = Path.home() / ".claude" / ".credentials.json"  # Linux fallback
API_URL = "https://api.anthropic.com/v1/messages"
API_HEADERS = {
    "anthropic-version": "2023-06-01",
    "anthropic-beta": "oauth-2025-04-20",
    "Content-Type": "application/json",
    "User-Agent": "claude-code/2.1.5",
}
API_BODY = {"model": "claude-haiku-4-5-20251001", "max_tokens": 1,
            "messages": [{"role": "user", "content": "hi"}]}

# Canonical state vocabulary (v2 — clawpet). Lower-case, short, BLE-friendly.
STATES = (
    "idle", "listening", "thinking", "working", "searching", "browsing",
    "danger", "permission", "denied", "bored", "authok", "done", "subdone",
    "error", "rate", "compacting", "touched", "dizzy", "sleep", "speaking",
)

# Claude Code event → face state. Mapping lives here so hooks stay dumb
# (PreToolUse routing is split by matcher in ~/.claude/settings.json; the
# daemon only sees the resulting event arg). Verified vs Claude Code v2.1.159.
EVENT_MAP = {
    "session-start":      "idle",         # SessionStart
    "prompt-submit":      "listening",    # UserPromptSubmit → brief listening, then thinking (see handle_command)
    "edit":               "working",      # PreToolUse · Edit|Write
    "search":             "searching",    # PreToolUse · Read|Grep|Glob
    "browse":             "browsing",     # PreToolUse · WebFetch|WebSearch
    "bash":               "working",      # PreToolUse · Bash (non-dangerous)
    "danger":             "danger",       # PreToolUse · Bash if(rm -rf / force push)
    "post-tool":          "working",      # PostToolUse (stay in work)
    "tool-failure":       "error",        # PostToolUseFailure
    "permission-request": "permission",   # PermissionRequest (needs user OK)
    "permission-denied":  "denied",       # PermissionDenied
    "idle-prompt":        "bored",        # Notification · idle_prompt
    "auth-ok":            "authok",        # Notification · auth_success
    "subagent-stop":      "subdone",      # SubagentStop
    "stop":               "done",         # Stop (turn finished)
    "stop-failure":       "rate",         # StopFailure (API error, not a code error)
    "precompact":         "compacting",   # PreCompact
    "session-end":        "sleep",        # SessionEnd
    # legacy aliases (older hooks / docs)
    "pre-tool":           "working",
    "notification":       "permission",
    "error":              "error",
    "idle":               "idle",
}

# Transient states auto-fall-back to idle after a short hold.
TRANSIENT = {"done", "subdone", "authok", "error", "rate",
             "denied", "danger", "touched", "dizzy"}
REVERT_SECONDS = 4.0
DANGER_STICKY_SECONDS = 3.0   # danger ignores lower-priority refreshes this long
LISTEN_SECONDS = 0.7          # prompt-submit shows `listening` this long, then thinking

# Priority: higher wins when deciding what to show right now. During the danger
# sticky window any incoming state below danger's priority is dropped, so the
# alarmed face holds through the Bash that follows the rm -rf.
PRIORITY = {
    "danger": 100, "permission": 90, "denied": 70,
    "touched": 80, "dizzy": 80,
    "error": 60, "rate": 60, "done": 50, "subdone": 50, "authok": 50,
    "compacting": 30, "browsing": 20, "searching": 20, "working": 20,
    "speaking": 18, "thinking": 15, "bored": 12, "listening": 12,
    "sleep": 10, "idle": 5,
}


def _priority(s: str) -> int:
    return PRIORITY.get(s, 5)

RUN_DIR = os.path.expanduser("~/.deskbuddy")
SOCK_PATH = os.path.join(RUN_DIR, "sock")
LOG_PATH = os.path.join(RUN_DIR, "deskbuddy.log")


def _ensure_run_dir():
    os.makedirs(RUN_DIR, exist_ok=True)


# ─── Usage fetch (blocking; run via asyncio.to_thread) ───────────────────────
def _extract_token(blob: str):
    blob = blob.strip()
    if not blob:
        return None
    try:
        data = json.loads(blob)
    except json.JSONDecodeError:
        data = None
    if isinstance(data, dict):
        if isinstance(data.get("accessToken"), str):
            return data["accessToken"]
        for v in data.values():
            if isinstance(v, dict) and isinstance(v.get("accessToken"), str):
                return v["accessToken"]
    m = re.search(r'"accessToken"\s*:\s*"([^"]+)"', blob)
    if m:
        return m.group(1)
    if re.fullmatch(r"[A-Za-z0-9_\-.~+/=]{20,}", blob):
        return blob
    return None


def _read_token():
    if sys.platform == "darwin":
        try:
            out = subprocess.run(
                ["security", "find-generic-password", "-s", KEYCHAIN_SERVICE,
                 "-a", getpass.getuser(), "-w"],
                check=True, capture_output=True, text=True, timeout=10)
            return _extract_token(out.stdout)
        except Exception:
            return None
    try:
        return _extract_token(CREDENTIALS_PATH.read_text())
    except OSError:
        return None


def _fetch_usage():
    """Blocking: read token, hit the API, return {s,sr,w,wr,st,ok} or None."""
    token = _read_token()
    if not token:
        return None
    headers = dict(API_HEADERS); headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(API_URL, data=json.dumps(API_BODY).encode(),
                                 headers=headers, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=20) as resp:
            h = resp.headers
    except urllib.error.HTTPError as e:
        h = e.headers              # rate-limit headers are present on errors too
    except Exception:
        return None
    now = time.time()

    def reset_minutes(ts):
        try:
            m = (float(ts) - now) / 60.0
        except (TypeError, ValueError):
            return 0
        return int(round(m)) if m > 0 else 0

    def pct(util):
        try:
            return int(round(float(util) * 100))
        except (TypeError, ValueError):
            return 0

    return {
        "s":  pct(h.get("anthropic-ratelimit-unified-5h-utilization")),
        "sr": reset_minutes(h.get("anthropic-ratelimit-unified-5h-reset")),
        "w":  pct(h.get("anthropic-ratelimit-unified-7d-utilization")),
        "wr": reset_minutes(h.get("anthropic-ratelimit-unified-7d-reset")),
        "st": h.get("anthropic-ratelimit-unified-5h-status", "unknown"),
        "ok": True,
    }


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
        self.follow_task = None        # pending one-shot state advance (listening→thinking)
        self.danger_until = 0.0        # monotonic deadline of danger sticky window

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
        now = time.monotonic()
        # Danger sticky: while the alarm window is open, drop any lower-priority
        # refresh (e.g. the working/bash that follows the rm -rf) so the danger
        # face holds. Equal/higher priority (a real permission prompt) still wins.
        if now < self.danger_until and _priority(state) < PRIORITY["danger"]:
            self.log(f"danger sticky: ignore {state}")
            return
        self.desired = state
        self.change.set()
        # Any new state cancels a pending listening→thinking follow-up.
        if self.follow_task:
            self.follow_task.cancel()
            self.follow_task = None
        # Cancel any pending auto-revert; schedule a fresh one for transients.
        if self.revert_task:
            self.revert_task.cancel()
            self.revert_task = None
        if state == "danger":
            self.danger_until = now + DANGER_STICKY_SECONDS
            self.revert_task = asyncio.create_task(self._revert_later(DANGER_STICKY_SECONDS))
        elif state in TRANSIENT:
            self.revert_task = asyncio.create_task(self._revert_later(REVERT_SECONDS))

    async def _revert_later(self, delay: float):
        try:
            await asyncio.sleep(delay)
        except asyncio.CancelledError:
            return
        self.desired = "idle"
        self.change.set()
        self.log("auto-revert -> idle")

    async def _follow_later(self, state: str, delay: float):
        # One-shot advance to `state` unless a new event arrives first.
        try:
            await asyncio.sleep(delay)
        except asyncio.CancelledError:
            return
        self.follow_task = None        # clear before re-entering apply_state
        self.apply_state(state)

    def handle_command(self, line: str) -> None:
        parts = line.split()
        if not parts:
            return
        verb = parts[0]
        arg = parts[1] if len(parts) > 1 else ""
        if verb == "state":
            self.apply_state(arg)
        elif verb == "event":
            if arg == "prompt-submit":
                # Show `listening` for a beat, then settle into `thinking`.
                self.apply_state("listening")
                self.follow_task = asyncio.create_task(
                    self._follow_later("thinking", LISTEN_SECONDS))
                return
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
                    usage_task = asyncio.create_task(self.usage_loop(client))
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
                    usage_task.cancel()
            except Exception as e:
                self.log(f"ble error: {e}")
            finally:
                self.connected = False
            await asyncio.sleep(2)  # reconnect backoff

    async def usage_loop(self, client):
        """Poll usage and write it to the RX char while connected (original UI)."""
        while True:
            try:
                payload = await asyncio.to_thread(_fetch_usage)
                if payload is not None:
                    await client.write_gatt_char(
                        RX_CHAR, json.dumps(payload, separators=(",", ":")).encode(),
                        response=False)
                    self.log(f"usage -> s={payload['s']}% w={payload['w']}% "
                             f"({payload['st']})")
                else:
                    self.log("usage: no token / fetch failed")
            except asyncio.CancelledError:
                return
            except Exception as e:
                self.log(f"usage error: {e}")
            try:
                await asyncio.sleep(USAGE_POLL_SECONDS)
            except asyncio.CancelledError:
                return

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
