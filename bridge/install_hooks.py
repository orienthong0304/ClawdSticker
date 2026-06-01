#!/usr/bin/env python3
"""Idempotently merge ClawdSticker's Claude Code hooks into settings.json.

Usage:  python3 install_hooks.py /abs/path/to/bridge/deskbuddy

- Backs up settings.json before touching it.
- Adds one hook per Claude Code event, each calling `deskbuddy event <name>`.
- Re-runnable: it first strips ANY previously-installed deskbuddy hooks (by
  matching the `/bridge/deskbuddy` command), so a second run just refreshes the
  path. Hooks that aren't ours (e.g. other tools) are left untouched.

Settings path defaults to ~/.claude/settings.json; override with $CLAUDE_SETTINGS
(used by the test fixture).

Event names verified against Claude Code v2.1.159 — re-check the official hooks
docs if a future version renames them.
"""

import json
import os
import shutil
import sys
import time
from pathlib import Path

if len(sys.argv) < 2:
    sys.exit("usage: install_hooks.py /abs/path/to/bridge/deskbuddy")

DESKBUDDY = sys.argv[1]
SETTINGS = Path(os.environ.get("CLAUDE_SETTINGS", Path.home() / ".claude" / "settings.json"))

# Marker used both to write and to recognise our own hooks on re-runs.
MARKER = "/bridge/deskbuddy"


def _cmd(event):
    return {"type": "command",
            "command": f"{DESKBUDDY} event {event}",
            "timeout": 5,
            "async": True}


def _group(event, matcher=None):
    g = {"hooks": [_cmd(event)]}
    if matcher:
        g["matcher"] = matcher
    return g


# event name in settings.json -> the groups we install for it
HOOKS = {
    "SessionStart":       [_group("session-start")],
    "UserPromptSubmit":   [_group("prompt-submit")],
    "PreToolUse":         [_group("edit",   "Edit|Write"),
                           _group("search", "Read|Grep|Glob"),
                           _group("browse", "WebFetch|WebSearch"),
                           _group("bash",   "Bash")],
    "PostToolUse":        [_group("post-tool")],
    "PostToolUseFailure": [_group("tool-failure")],
    "Notification":       [_group("idle-prompt", "idle_prompt"),
                           _group("auth-ok",     "auth_success")],
    "PermissionRequest":  [_group("permission-request")],
    "PermissionDenied":   [_group("permission-denied")],
    "SubagentStop":       [_group("subagent-stop")],
    "Stop":               [_group("stop")],
    "StopFailure":        [_group("stop-failure")],
    "SessionEnd":         [_group("session-end")],
    "PreCompact":         [_group("precompact", "*")],
}


def _is_ours(hook):
    return MARKER in hook.get("command", "")


def main():
    SETTINGS.parent.mkdir(parents=True, exist_ok=True)
    if SETTINGS.exists():
        data = json.loads(SETTINGS.read_text() or "{}")
        backup = SETTINGS.with_suffix(f".json.bak.{int(time.time())}")
        shutil.copy(SETTINGS, backup)
        print(f"  backed up → {backup.name}")
    else:
        data = {}

    hooks = data.setdefault("hooks", {})

    # 1) strip our previously-installed hooks (keep everyone else's)
    stripped = 0
    for event in list(hooks.keys()):
        new_groups = []
        for g in hooks[event]:
            kept = [h for h in g.get("hooks", []) if not _is_ours(h)]
            stripped += len(g.get("hooks", [])) - len(kept)
            if kept:
                g["hooks"] = kept
                new_groups.append(g)
        if new_groups:
            hooks[event] = new_groups
        else:
            del hooks[event]

    # 2) add our fresh set
    added = 0
    for event, groups in HOOKS.items():
        hooks.setdefault(event, [])
        hooks[event].extend(groups)
        added += sum(len(g["hooks"]) for g in groups)

    SETTINGS.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n")
    print(f"  hooks: removed {stripped} stale, added {added} → {SETTINGS}")


if __name__ == "__main__":
    main()
