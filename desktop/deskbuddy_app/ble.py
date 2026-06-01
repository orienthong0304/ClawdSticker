"""BleLink — owns the bleak connection to the device, writes STATE_CHAR.

Lifted from bridge/deskbuddy.py's `ble_loop`. Runs entirely on the asyncio loop
thread. The controller pushes the desired state via `push()`; the writer
coalesces (only writes on change) and re-pushes on every reconnect.

Usage data is NO LONGER fetched here — the controller polls it host-side (see
usage.py) and hands us each payload via `write_usage()`, which we forward to the
device's original usage screen only while connected.
"""

import asyncio
import json

from .engine import DEVICE_NAME, STATE_CHAR, RX_CHAR


class BleLink:
    def __init__(self, log, on_connection):
        self.log = log
        self.on_connection = on_connection          # (connected: bool) -> None
        self.desired = "idle"
        self.connected = False
        self.change = asyncio.Event()
        self._want = True                            # whether we want to stay connected
        self._client = None                          # live BleakClient while connected
        self.last_usage = None                       # latest usage payload (re-pushed on reconnect)

    def push(self, state: str):
        self.desired = state
        self.change.set()

    async def write_usage(self, payload):
        """Forward a usage payload to the device's RX_CHAR (only while connected)."""
        self.last_usage = payload
        if not (self.connected and self._client and payload is not None):
            return
        try:
            await self._client.write_gatt_char(
                RX_CHAR, json.dumps(payload, separators=(",", ":")).encode(),
                response=False)
        except Exception as e:
            self.log(f"usage write failed: {e}")

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
                    self._client = client
                    self.on_connection(True)
                    self.log("connected")
                    last = None
                    self.change.set()                # push current state on (re)connect
                    if self.last_usage is not None:  # refresh the device's usage screen
                        await self.write_usage(self.last_usage)
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
            except Exception as e:
                self.log(f"ble error: {e}")
            finally:
                self._client = None
                if self.connected:
                    self.connected = False
                    self.on_connection(False)
            await asyncio.sleep(2)                    # reconnect backoff
