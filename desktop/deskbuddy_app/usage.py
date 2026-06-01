"""usage.py — host-side token-usage fetcher (decoupled from BLE).

Reuses `_fetch_usage` from the existing bridge module (single implementation:
reads the Keychain OAuth token, hits the Anthropic API, returns the rate-limit
payload `{"s","sr","w","wr","st","ok"}` or None). Lives here so the controller
can poll it on an always-on loop — the usage page works even when the device is
offline, and BLE just piggybacks the cached payload to the device when connected.
"""

import asyncio
import os
import sys

# Optional reuse of the existing bridge's usage fetcher (single source of truth).
_fetch_usage = None
try:
    _bridge_dir = os.path.join(
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
        "bridge")
    if _bridge_dir not in sys.path:
        sys.path.insert(0, _bridge_dir)
    from deskbuddy import _fetch_usage as _fetch_usage  # type: ignore
except Exception:
    _fetch_usage = None


def available() -> bool:
    return _fetch_usage is not None


async def fetch():
    """Return the usage payload dict, or None (no token / API error / unavailable)."""
    if _fetch_usage is None:
        return None
    try:
        return await asyncio.to_thread(_fetch_usage)
    except Exception:
        return None
