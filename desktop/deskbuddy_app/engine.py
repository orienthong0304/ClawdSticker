"""Face state engine — the single source of the state vocabulary + arbitration.

This is the loop-agnostic decision core, lifted from bridge/deskbuddy.py's
`Bridge` and given a `source` tag on every transition so the controller can
arbitrate manual-vs-hook and log who drove a change. It owns NO BLE and NO
socket — it only decides what the device *should* show, and fires `on_change`.

`apply_state` / `handle_event` MUST be called on the asyncio loop thread:
they schedule auto-revert / follow-up via `asyncio.create_task` (same as the
original bridge). The controller guarantees this (all inbound paths funnel
through `run_coroutine_threadsafe` or the loop's own socket server).
"""

import asyncio
import time

# ─── Protocol / BLE constants (mirrors bridge/deskbuddy.py) ──────────────────
DEVICE_NAME = "Claude Controller"
STATE_CHAR = "4c41555a-4465-7669-6365-000000000005"   # face-state string (write)
RX_CHAR    = "4c41555a-4465-7669-6365-000000000002"   # usage JSON (original UI)
USAGE_POLL_SECONDS = 300

# Canonical 20-state vocabulary (v2 clawpet) — must match firmware face.cpp kNames[].
STATES = (
    "idle", "listening", "thinking", "working", "searching", "browsing",
    "danger", "permission", "denied", "bored", "authok", "done", "subdone",
    "error", "rate", "compacting", "touched", "dizzy", "sleep", "speaking",
)

# Claude Code event → face state.
EVENT_MAP = {
    "session-start":      "idle",
    "prompt-submit":      "listening",    # → brief listening, then thinking
    "edit":               "working",
    "search":             "searching",
    "browse":             "browsing",
    "bash":               "working",
    "danger":             "danger",
    "post-tool":          "working",
    "tool-failure":       "error",
    "permission-request": "permission",
    "permission-denied":  "denied",
    "idle-prompt":        "bored",
    "auth-ok":            "authok",
    "subagent-stop":      "subdone",
    "stop":               "done",
    "stop-failure":       "rate",
    "precompact":         "compacting",
    "session-end":        "sleep",
    # legacy aliases
    "pre-tool":           "working",
    "notification":       "permission",
    "error":              "error",
    "idle":               "idle",
}

# Transient states auto-fall-back to idle after a short hold.
TRANSIENT = {"done", "subdone", "authok", "error", "rate",
             "denied", "danger", "touched", "dizzy"}
REVERT_SECONDS = 4.0
DANGER_STICKY_SECONDS = 3.0
LISTEN_SECONDS = 0.7

PRIORITY = {
    "danger": 100, "permission": 90, "denied": 70,
    "touched": 80, "dizzy": 80,
    "error": 60, "rate": 60, "done": 50, "subdone": 50, "authok": 50,
    "compacting": 30, "browsing": 20, "searching": 20, "working": 20,
    "speaking": 18, "thinking": 15, "bored": 12, "listening": 12,
    "sleep": 10, "idle": 5,
}


def priority(s: str) -> int:
    return PRIORITY.get(s, 5)


class FaceEngine:
    """Decides the desired face state; fires on_change(state, source)."""

    def __init__(self, on_change, log):
        self.on_change = on_change          # (state: str, source: str) -> None
        self.log = log
        self.desired = "idle"
        self.revert_task = None
        self.follow_task = None             # pending listening→thinking advance
        self.danger_until = 0.0             # monotonic deadline of danger sticky window

    def apply_state(self, state: str, *, source: str) -> bool:
        if state not in STATES:
            self.log(f"ignore unknown state: {state!r}")
            return False
        now = time.monotonic()
        # Danger sticky: hold the alarm through any lower-priority refresh.
        if now < self.danger_until and priority(state) < PRIORITY["danger"]:
            self.log(f"danger sticky: ignore {state}")
            return False
        self.desired = state
        self.on_change(state, source)
        if self.follow_task:
            self.follow_task.cancel()
            self.follow_task = None
        if self.revert_task:
            self.revert_task.cancel()
            self.revert_task = None
        if state == "danger":
            self.danger_until = now + DANGER_STICKY_SECONDS
            self.revert_task = asyncio.create_task(self._revert_later(DANGER_STICKY_SECONDS))
        elif state in TRANSIENT:
            self.revert_task = asyncio.create_task(self._revert_later(REVERT_SECONDS))
        return True

    async def _revert_later(self, delay: float):
        try:
            await asyncio.sleep(delay)
        except asyncio.CancelledError:
            return
        self.desired = "idle"
        self.on_change("idle", "auto-revert")
        self.log("auto-revert -> idle")

    async def _follow_later(self, state: str, delay: float):
        try:
            await asyncio.sleep(delay)
        except asyncio.CancelledError:
            return
        self.follow_task = None
        self.apply_state(state, source="follow")

    def handle_event(self, name: str, *, source: str) -> bool:
        if name == "prompt-submit":
            self.apply_state("listening", source=source)
            self.follow_task = asyncio.create_task(
                self._follow_later("thinking", LISTEN_SECONDS))
            return True
        mapped = EVENT_MAP.get(name)
        if mapped:
            return self.apply_state(mapped, source=source)
        self.log(f"ignore unknown event: {name!r}")
        return False
