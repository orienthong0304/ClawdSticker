"""app.py — process bootstrap.

Starts the asyncio loop in a background thread (BLE + hook socket + engine),
opens the pywebview console window on the main thread, and installs a menu-bar
tray so closing the window only HIDES it — the asyncio thread (and therefore
Claude Code hooks) keeps running. The tray "退出" fully quits.
"""

import asyncio
import os
import sys
import threading

import webview

from .controller import AppController
from .api import JsApi
from .bridge_to_ui import UiBridge
from .tray import Tray


def _web_index():
    here = os.path.dirname(os.path.abspath(__file__))
    # dev layout: desktop/deskbuddy_app/ + desktop/web/  ; bundled: <_MEIPASS>/web/
    for base in (os.path.dirname(here), getattr(sys, "_MEIPASS", here)):
        p = os.path.join(base, "web", "index.html")
        if os.path.exists(p):
            return p
    raise FileNotFoundError("web/index.html not found")


def _set_accessory_policy():
    """Run as a menu-bar accessory (no Dock icon). LSUIElement in the bundle's
    Info.plist handles this for the packaged app, but pywebview forces a Regular
    activation policy at startup, so we flip it back here once the page loads."""
    try:
        from AppKit import NSApplication, NSApplicationActivationPolicyAccessory
        NSApplication.sharedApplication().setActivationPolicy_(
            NSApplicationActivationPolicyAccessory)
    except Exception:
        pass


def main():
    loop = asyncio.new_event_loop()
    controller = AppController(loop)

    def run_loop():
        asyncio.set_event_loop(loop)
        loop.run_until_complete(controller.start())
        loop.run_forever()

    threading.Thread(target=run_loop, daemon=True, name="asyncio").start()

    # Autostart (LaunchAgent) passes --hidden so login doesn't pop the console;
    # the window stays hidden and only the menu-bar item appears.
    hidden = ("--hidden" in sys.argv) or bool(os.environ.get("DESKBUDDY_HIDDEN"))

    api = JsApi(controller, loop)
    window = webview.create_window(
        "ClawdSticker",
        url=_web_index(),
        js_api=api,
        width=560,
        height=940,
        min_size=(460, 600),
        background_color="#070809",
        hidden=hidden,
    )
    ui = UiBridge(window)
    controller.attach_ui(ui)

    # Only push to the page once it's loaded (early evaluate_js blocks the loop).
    def on_loaded():
        ui.ready = True
        controller.resync_ui()
        _set_accessory_policy()        # menu-bar app: no Dock icon (overrides pywebview)
    window.events.loaded += on_loaded

    # Close = hide (keep the asyncio thread + hooks alive); tray re-shows / quits.
    def on_closing():
        window.hide()
        return False
    window.events.closing += on_closing

    # Tray must be created on the main thread, before the Cocoa loop blocks.
    tray = Tray(window)
    controller.attach_tray(tray)
    webview.start(gui="cocoa")


if __name__ == "__main__":
    main()
