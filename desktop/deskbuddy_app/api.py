"""JsApi — the pywebview js_api object the web UI calls (on the GUI thread).

Every device-touching method hands a coroutine to the asyncio loop via
run_coroutine_threadsafe; only read-only status is answered synchronously.
"""

import asyncio


class JsApi:
    def __init__(self, controller, loop):
        self._c = controller
        self._loop = loop

    def _spawn(self, coro):
        """Fire-and-forget onto the asyncio loop (snappy UI)."""
        asyncio.run_coroutine_threadsafe(coro, self._loop)

    def _call(self, coro, timeout=2.0):
        """Run on the loop and wait for the result."""
        return asyncio.run_coroutine_threadsafe(coro, self._loop).result(timeout)

    # ─── methods exposed to JS as window.pywebview.api.* ──────────────────────
    def set_state(self, state):
        self._spawn(self._c.manual_set_state(state))
        return {"ok": True, "state": state}

    def set_mode(self, mode):
        try:
            return {"mode": self._call(self._c.set_mode(mode))}
        except Exception:
            return {"mode": self._c.mode}

    def connect(self):
        self._spawn(self._c.ble_connect())
        return True

    def disconnect(self):
        self._spawn(self._c.ble_disconnect())
        return True

    def get_status(self):
        return self._c.status()
