"""UiBridge — pushes device→UI events into the webview via evaluate_js.

Called from the asyncio-loop thread. pywebview marshals evaluate_js to the GUI
thread internally. All payloads are json.dumps-escaped into the JS call.
"""

import base64
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

    def on_usage(self, payload):
        # payload is a dict or None; json.dumps handles both.
        self._js("dbOnUsage", payload)

    def snapshot_face(self, state):
        """Render the current face state to a PNG via the web renderer; bytes or None.

        Runs the JS faceSnapshot() in the webview and decodes its data URL. Used
        by the menu-bar tray. Returns None before the page is ready or on error.
        """
        if not self.ready:
            return None
        try:
            url = self._w.evaluate_js(
                f"window.faceSnapshot && window.faceSnapshot({json.dumps(state)}, 2)")
            if not url or "," not in url:
                return None
            return base64.b64decode(url.split(",", 1)[1])
        except Exception:
            return None

    def snapshot_frames(self, state, count=20):
        """Render one animation loop of `state` as a list of PNG byte strings.

        One evaluate_js round-trip returns all frame data URLs. Used by the tray
        to animate the face while its menu is open. Returns None if not ready/err.
        """
        if not self.ready:
            return None
        try:
            urls = self._w.evaluate_js(
                f"window.faceFrames && window.faceFrames({json.dumps(state)}, 2, {int(count)}, 2.2)")
            if not urls:
                return None
            out = []
            for u in urls:
                if u and "," in u:
                    out.append(base64.b64decode(u.split(",", 1)[1]))
            return out or None
        except Exception:
            return None
