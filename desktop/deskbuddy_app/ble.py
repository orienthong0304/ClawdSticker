"""BleLink — owns the bleak connection to the device, writes STATE_CHAR.

Lifted from bridge/deskbuddy.py's `ble_loop` + `usage_loop`. Runs entirely on
the asyncio loop thread. The controller pushes the desired state via `push()`;
the writer coalesces (only writes on change) and re-pushes on every reconnect.

Usage polling (keeps the device's original usage screen alive) reuses
`_fetch_usage` from the existing bridge module if importable; otherwise it is
silently skipped — the desk-buddy face never depends on it.
"""

import asyncio
import json
import os
import sys

from .engine import DEVICE_NAME, STATE_CHAR, RX_CHAR, USAGE_POLL_SECONDS

# Optional reuse of the existing bridge's usage fetcher (single implementation).
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


class BleLink:
    def __init__(self, log, on_connection):
        self.log = log
        self.on_connection = on_connection          # (connected: bool) -> None
        self.desired = "idle"
        self.connected = False
        self.change = asyncio.Event()
        self._want = True                            # whether we want to stay connected

    def push(self, state: str):
        self.desired = state
        self.change.set()

    async def connect(self):
        self._want = True
        self.change.set()

    async def disconnect(self):
        self._want = False
        self.change.set()

    async def run(self):
        from bleak import BleakScanner, BleakClient
        while True:
            if not self._want:
                await asyncio.sleep(0.5)
                continue
            try:
                self.log(f"scanning for '{DEVICE_NAME}'...")
                dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=12)
                if not dev:
                    await asyncio.sleep(3)
                    continue
                self.log(f"connecting to {dev.address}")
                async with BleakClient(dev) as client:
                    self.connected = True
                    self.on_connection(True)
                    self.log("connected")
                    last = None
                    self.change.set()                # push current state on (re)connect
                    usage_task = asyncio.create_task(self._usage_loop(client))
                    while client.is_connected and self._want:
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
                if self.connected:
                    self.connected = False
                    self.on_connection(False)
            await asyncio.sleep(2)                    # reconnect backoff

    async def _usage_loop(self, client):
        if _fetch_usage is None:
            return
        while True:
            try:
                payload = await asyncio.to_thread(_fetch_usage)
                if payload is not None:
                    await client.write_gatt_char(
                        RX_CHAR, json.dumps(payload, separators=(",", ":")).encode(),
                        response=False)
                    self.log(f"usage -> s={payload['s']}% w={payload['w']}% ({payload['st']})")
            except asyncio.CancelledError:
                return
            except Exception as e:
                self.log(f"usage error: {e}")
            try:
                await asyncio.sleep(USAGE_POLL_SECONDS)
            except asyncio.CancelledError:
                return
