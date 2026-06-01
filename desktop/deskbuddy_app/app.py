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

_tray_refs = []          # keep AppKit tray objects alive (avoid GC)


def _web_index():
    here = os.path.dirname(os.path.abspath(__file__))
    # dev layout: desktop/deskbuddy_app/ + desktop/web/  ; bundled: <_MEIPASS>/web/
    for base in (os.path.dirname(here), getattr(sys, "_MEIPASS", here)):
        p = os.path.join(base, "web", "index.html")
        if os.path.exists(p):
            return p
    raise FileNotFoundError("web/index.html not found")


def _setup_tray(window):
    """Best-effort macOS menu-bar item. Failure is non-fatal (window still works)."""
    try:
        from AppKit import (NSStatusBar, NSMenu, NSMenuItem, NSApplication,
                            NSVariableStatusItemLength)
        from Foundation import NSObject

        class TrayHandler(NSObject):
            def show_(self, sender):
                try:
                    window.show()
                except Exception:
                    pass

            def quit_(self, sender):
                NSApplication.sharedApplication().terminate_(None)

        bar = NSStatusBar.systemStatusBar()
        item = bar.statusItemWithLength_(NSVariableStatusItemLength)
        try:
            item.button().setTitle_("🐾")
        except Exception:
            pass
        menu = NSMenu.alloc().init()
        handler = TrayHandler.alloc().init()
        show = NSMenuItem.alloc().initWithTitle_action_keyEquivalent_("显示控制台", "show:", "")
        show.setTarget_(handler)
        quit_item = NSMenuItem.alloc().initWithTitle_action_keyEquivalent_("退出", "quit:", "q")
        quit_item.setTarget_(handler)
        menu.addItem_(show)
        menu.addItem_(NSMenuItem.separatorItem())
        menu.addItem_(quit_item)
        item.setMenu_(menu)
        _tray_refs.extend([bar, item, menu, handler])
    except Exception as e:
        print(f"tray unavailable: {e}", flush=True)


def main():
    loop = asyncio.new_event_loop()
    controller = AppController(loop)

    def run_loop():
        asyncio.set_event_loop(loop)
        loop.run_until_complete(controller.start())
        loop.run_forever()

    threading.Thread(target=run_loop, daemon=True, name="asyncio").start()

    api = JsApi(controller, loop)
    window = webview.create_window(
        "Clawdmeter Console",
        url=_web_index(),
        js_api=api,
        width=560,
        height=940,
        min_size=(460, 600),
        background_color="#070809",
    )
    ui = UiBridge(window)
    controller.attach_ui(ui)

    # Only push to the page once it's loaded (early evaluate_js blocks the loop).
    def on_loaded():
        ui.ready = True
        controller.resync_ui()
    window.events.loaded += on_loaded

    # Close = hide (keep the asyncio thread + hooks alive); tray re-shows / quits.
    def on_closing():
        window.hide()
        return False
    window.events.closing += on_closing

    # Tray must be created on the main thread, before the Cocoa loop blocks.
    _setup_tray(window)
    webview.start(gui="cocoa")


if __name__ == "__main__":
    main()
