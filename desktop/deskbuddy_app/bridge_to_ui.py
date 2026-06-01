"""UiBridge — pushes device→UI events into the webview via evaluate_js.

Called from the asyncio-loop thread. pywebview marshals evaluate_js to the GUI
thread internally. All payloads are json.dumps-escaped into the JS call.
"""

import json


class UiBridge:
    def __init__(self, window):
        self._w = window
        self.ready = False          # set True on the window's `loaded` event

    def _js(self, fn, *args):
        # Before the page is loaded, evaluate_js blocks (stalling the asyncio
        # loop / BLE). Drop early pushes — console.js hydrates via get_status,
        # and the controller resyncs once `loaded` flips ready True.
        if not self.ready:
            return
        payload = ",".join(json.dumps(a) for a in args)
        code = f"window.{fn} && window.{fn}({payload});"
        try:
            self._w.evaluate_js(code)
        except Exception:
            pass

    def on_connection_change(self, connected):
        self._js("dbOnConnection", bool(connected))

    def on_state_change(self, state, source):
        self._js("dbOnState", state, source)

    def on_log(self, line):
        self._js("dbOnLog", line)

    def on_mode(self, mode):
        self._js("dbOnMode", mode)
