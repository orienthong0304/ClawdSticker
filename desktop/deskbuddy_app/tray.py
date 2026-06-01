"""tray.py — menu-bar (NSStatusItem) item: live animated face + usage.

The status-bar item shows either 🐾 or the live token-usage percentages (a
user setting). Clicking it opens a menu whose top item is a custom view:
  • the current face — animated while the menu is open (frames pre-rendered by
    the web renderer and handed in as PNG bytes; cycled by an NSTimer),
  • the state label (emoji + 中文) and device connection line,
  • two percentage bars (5h / 7d) each with a reset countdown,
then three checkable settings (show usage in the menu bar · 5h · 7d), then
显示控制台 / 退出.

Settings persist to ~/.deskbuddy/app.json (reusing the controller's helpers).

Thread model — the only safe one for Cocoa:
  • `set_state` / `set_frames` / `set_usage` are called from the asyncio thread.
    They ONLY build plain data objects and stash them; `set_usage` then asks for
    a menu-bar title refresh which is marshalled to the main thread via
    AppHelper.callAfter.
  • All view/menu/status-item mutation happens on the MAIN thread: the NSMenu
    delegate (`menuNeedsUpdate:`/`menuWillOpen:`/`menuDidClose:`), the animation
    timer tick, the checkable-item actions, and the callAfter'd title setter.

All ObjC objects are kept on the instance (and in _tray_refs) so they aren't GC'd.
"""

from .engine import STATE_LABEL, STATE_EMOJI
from .controller import _load_pref, _save_pref

# Quiet a harmless pyobjc warning emitted whenever we read a CGColor (used for
# the bar fills / connected dot) — the pointer round-trips fine.
try:
    import objc
    import warnings
    warnings.filterwarnings("ignore", category=objc.ObjCPointerWarning)
except Exception:
    pass

_tray_refs = []          # module-level keepalive for ObjC objects

# bar fill colors by utilization threshold (r, g, b)
_BAR_LO = (0.18, 0.90, 0.63)
_BAR_MID = (1.00, 0.69, 0.13)
_BAR_HI = (1.00, 0.35, 0.35)
_TRACK_W = 204.0


def _bar_rgb(pct):
    return _BAR_LO if pct < 60 else (_BAR_MID if pct < 85 else _BAR_HI)


def _fmt_reset(m):
    """Minutes-until-reset → compact '24m' / '2h14m' / '1d8h' (or '—')."""
    if m is None or m < 0:
        return "—"
    if m < 60:
        return f"{m}m"
    if m < 1440:
        return f"{m // 60}h{m % 60}m"
    return f"{m // 1440}d{(m % 1440) // 60}h"


class Tray:
    def __init__(self, window):
        self.window = window
        self._image = None            # static fallback NSImage
        self._frames = []             # list[NSImage] for the current state
        self._frames_state = None
        self._anim_i = 0
        self._timer = None
        self._state = "idle"
        self._connected = False
        self._usage = None            # dict {s,sr,w,wr,st} or None
        self._ok = False
        # menu-bar display settings (persisted)
        self._mb_show = bool(_load_pref("mb_show", False))
        self._mb_5h = bool(_load_pref("mb_5h", True))
        self._mb_7d = bool(_load_pref("mb_7d", True))
        # view / item handles (filled by _build)
        self._item = None
        self._image_view = None
        self._state_label = None
        self._device_label = None
        self._s5_cap = self._s5_fill = self._s5_reset = None
        self._w7_cap = self._w7_fill = self._w7_reset = None
        self._mb_show_item = self._mb5_item = self._mb7_item = None
        self._build()

    # ── data updates (asyncio thread; NO view mutation here) ───────────────────
    def set_state(self, state, png_bytes, connected):
        self._state = state or self._state
        self._connected = bool(connected)
        if png_bytes:
            img = self._image_from_bytes(png_bytes)
            if img is not None:
                self._image = img

    def set_frames(self, state, frames_bytes):
        if not frames_bytes:
            return
        imgs = [self._image_from_bytes(b) for b in frames_bytes]
        imgs = [i for i in imgs if i is not None]
        if imgs:
            self._frames = imgs
            self._frames_state = state

    def set_usage(self, payload):
        self._usage = payload
        self.update_menubar()         # keep the menu-bar title in sync (main-thread hop)

    def _image_from_bytes(self, png_bytes):
        try:
            from Foundation import NSData
            from AppKit import NSImage
            data = NSData.dataWithBytes_length_(png_bytes, len(png_bytes))
            return NSImage.alloc().initWithData_(data)
        except Exception:
            return None

    # ── build the status item + menu (main thread, at startup) ─────────────────
    def _build(self):
        try:
            from AppKit import (
                NSStatusBar, NSMenu, NSMenuItem, NSApplication, NSView, NSImageView,
                NSTextField, NSFont, NSColor, NSVariableStatusItemLength,
                NSImageScaleProportionallyUpOrDown, NSTextAlignmentCenter,
                NSTextAlignmentRight, NSRunLoop, NSRunLoopCommonModes, NSTimer)
            from Foundation import NSObject, NSMakeRect
            from PyObjCTools import AppHelper

            tray = self
            self._AppHelper = AppHelper

            def cap(s):
                lbl = NSTextField.labelWithString_(s)
                lbl.setFont_(NSFont.systemFontOfSize_(10))
                lbl.setTextColor_(NSColor.secondaryLabelColor())
                return lbl

            def reset_lbl(y):
                lbl = NSTextField.labelWithString_("")
                lbl.setFrame_(NSMakeRect(18, y, _TRACK_W, 13))
                lbl.setFont_(NSFont.systemFontOfSize_(10))
                lbl.setTextColor_(NSColor.tertiaryLabelColor())
                lbl.setAlignment_(NSTextAlignmentRight)
                return lbl

            def track(y):
                t = NSView.alloc().initWithFrame_(NSMakeRect(18, y, _TRACK_W, 7))
                t.setWantsLayer_(True)
                t.layer().setCornerRadius_(3.5)
                t.layer().setBackgroundColor_(
                    NSColor.colorWithCalibratedRed_green_blue_alpha_(1, 1, 1, .08).CGColor())
                f = NSView.alloc().initWithFrame_(NSMakeRect(0, 0, 0, 7))
                f.setWantsLayer_(True)
                f.layer().setCornerRadius_(3.5)
                t.addSubview_(f)
                return t, f

            class TrayHandler(NSObject):
                def show_(self, sender):
                    try:
                        tray.window.show()
                    except Exception:
                        pass

                def quit_(self, sender):
                    NSApplication.sharedApplication().terminate_(None)

                def mbShow_(self, sender):
                    tray._toggle("show")

                def mb5_(self, sender):
                    tray._toggle("5h")

                def mb7_(self, sender):
                    tray._toggle("7d")

            class MenuDelegate(NSObject):
                def menuNeedsUpdate_(self, menu):
                    tray._apply_to_views()

                def menuWillOpen_(self, menu):
                    tray._start_anim()

                def menuDidClose_(self, menu):
                    tray._stop_anim()

                def animTick_(self, timer):
                    tray._anim_tick()

            CW, CH = 240.0, 344.0
            container = NSView.alloc().initWithFrame_(NSMakeRect(0, 0, CW, CH))

            iv = NSImageView.alloc().initWithFrame_(NSMakeRect(30, 124, 180, 210))
            iv.setImageScaling_(NSImageScaleProportionallyUpOrDown)
            iv.setWantsLayer_(True)
            try:
                iv.layer().setCornerRadius_(18.0)
                iv.layer().setMasksToBounds_(True)
            except Exception:
                pass

            state_label = NSTextField.labelWithString_("")
            state_label.setFrame_(NSMakeRect(10, 92, CW - 20, 22))
            state_label.setAlignment_(NSTextAlignmentCenter)
            state_label.setFont_(NSFont.boldSystemFontOfSize_(14))
            state_label.setTextColor_(NSColor.whiteColor())

            device_label = NSTextField.labelWithString_("")
            device_label.setFrame_(NSMakeRect(10, 72, CW - 20, 16))
            device_label.setAlignment_(NSTextAlignmentCenter)
            device_label.setFont_(NSFont.systemFontOfSize_(11))
            device_label.setTextColor_(NSColor.secondaryLabelColor())

            s5_cap = cap("5 小时"); s5_cap.setFrame_(NSMakeRect(18, 51, 130, 13))
            s5_reset = reset_lbl(51)
            s5_track, s5_fill = track(42)
            w7_cap = cap("7 天");   w7_cap.setFrame_(NSMakeRect(18, 23, 130, 13))
            w7_reset = reset_lbl(23)
            w7_track, w7_fill = track(14)

            for v in (iv, state_label, device_label, s5_cap, s5_reset, s5_track,
                      w7_cap, w7_reset, w7_track):
                container.addSubview_(v)

            face_item = NSMenuItem.alloc().init()
            face_item.setView_(container)

            handler = TrayHandler.alloc().init()

            def mk(title, action):
                it = NSMenuItem.alloc().initWithTitle_action_keyEquivalent_(title, action, "")
                it.setTarget_(handler)
                return it

            mb_show_item = mk("在菜单栏显示余量", "mbShow:")
            mb5_item = mk("    5 小时余量", "mb5:")
            mb7_item = mk("    7 天余量", "mb7:")
            show = mk("显示控制台", "show:")
            quit_item = NSMenuItem.alloc().initWithTitle_action_keyEquivalent_("退出", "quit:", "q")
            quit_item.setTarget_(handler)

            menu = NSMenu.alloc().init()
            delegate = MenuDelegate.alloc().init()
            menu.setDelegate_(delegate)
            menu.addItem_(face_item)
            menu.addItem_(NSMenuItem.separatorItem())
            menu.addItem_(mb_show_item)
            menu.addItem_(mb5_item)
            menu.addItem_(mb7_item)
            menu.addItem_(NSMenuItem.separatorItem())
            menu.addItem_(show)
            menu.addItem_(quit_item)

            bar = NSStatusBar.systemStatusBar()
            item = bar.statusItemWithLength_(NSVariableStatusItemLength)
            item.setMenu_(menu)

            self._item = item
            self._image_view = iv
            self._state_label = state_label
            self._device_label = device_label
            self._s5_cap, self._s5_fill, self._s5_reset = s5_cap, s5_fill, s5_reset
            self._w7_cap, self._w7_fill, self._w7_reset = w7_cap, w7_fill, w7_reset
            self._mb_show_item, self._mb5_item, self._mb7_item = mb_show_item, mb5_item, mb7_item
            self._delegate = delegate
            self._NSRunLoop, self._NSRunLoopCommonModes, self._NSTimer = (
                NSRunLoop, NSRunLoopCommonModes, NSTimer)
            self._NSColor = NSColor
            self._ok = True
            _tray_refs.extend([bar, item, menu, delegate, handler, container, iv,
                               state_label, device_label, s5_cap, s5_reset, s5_track,
                               w7_cap, w7_reset, w7_track, face_item,
                               mb_show_item, mb5_item, mb7_item, show, quit_item])
            self._apply_menu_states()
            self._apply_title(self._menubar_title())
        except Exception as e:
            print(f"tray unavailable: {e}", flush=True)

    # ── view updates (main thread: via delegate / timer) ───────────────────────
    def _apply_to_views(self):
        self._anim_i = 0
        self._refresh()

    def _refresh(self):
        """Push current state/usage/frame into the views (main thread, cheap so
        it can run every animation tick — keeps the panel live while open)."""
        if not self._ok:
            return
        try:
            animating = self._frames and self._frames_state == self._state
            img = self._frames[self._anim_i % len(self._frames)] if animating else self._image
            if img is not None:
                self._image_view.setImage_(img)
            emoji = STATE_EMOJI.get(self._state, "")
            label = STATE_LABEL.get(self._state, self._state)
            self._state_label.setStringValue_(f"{emoji} {label}".strip())
            if self._connected:
                self._device_label.setStringValue_("● 已连接")
                self._device_label.setTextColor_(
                    self._NSColor.colorWithCalibratedRed_green_blue_alpha_(.18, .90, .63, 1))
            else:
                self._device_label.setStringValue_("○ 离线")
                self._device_label.setTextColor_(self._NSColor.secondaryLabelColor())
            self._apply_usage()
        except Exception:
            pass

    def _apply_usage(self):
        u = self._usage
        ok = bool(u) and u.get("ok", True) is not False
        from Foundation import NSMakeRect
        for cap_lbl, fill, reset_lbl, pkey, rkey, name in (
                (self._s5_cap, self._s5_fill, self._s5_reset, "s", "sr", "5 小时"),
                (self._w7_cap, self._w7_fill, self._w7_reset, "w", "wr", "7 天")):
            if ok and u.get(pkey) is not None:
                pct = max(0, min(100, int(u[pkey])))
                cap_lbl.setStringValue_(f"{name} · {pct}%")
                reset_lbl.setStringValue_(f"重置 {_fmt_reset(u.get(rkey))}")
                fill.setFrame_(NSMakeRect(0, 0, _TRACK_W * pct / 100.0, 7))
                r, g, b = _bar_rgb(pct)
                fill.layer().setBackgroundColor_(
                    self._NSColor.colorWithCalibratedRed_green_blue_alpha_(r, g, b, 1).CGColor())
            else:
                cap_lbl.setStringValue_(f"{name} · —")
                reset_lbl.setStringValue_("")
                fill.setFrame_(NSMakeRect(0, 0, 0, 7))

    # ── menu-bar settings + title ──────────────────────────────────────────────
    def _toggle(self, key):
        """Flip a menu-bar display setting (main thread; from a menu action)."""
        if key == "show":
            self._mb_show = not self._mb_show; _save_pref("mb_show", self._mb_show)
        elif key == "5h":
            self._mb_5h = not self._mb_5h; _save_pref("mb_5h", self._mb_5h)
        elif key == "7d":
            self._mb_7d = not self._mb_7d; _save_pref("mb_7d", self._mb_7d)
        self._apply_menu_states()
        self._apply_title(self._menubar_title())

    def _apply_menu_states(self):
        if not self._ok:
            return
        try:
            self._mb_show_item.setState_(1 if self._mb_show else 0)
            self._mb5_item.setState_(1 if self._mb_5h else 0)
            self._mb7_item.setState_(1 if self._mb_7d else 0)
        except Exception:
            pass

    def _menubar_title(self):
        u = self._usage
        if not self._mb_show or not u or u.get("ok", True) is False:
            return "🐾"
        parts = []
        if self._mb_5h and u.get("s") is not None:
            parts.append(f"5h {int(u['s'])}%")
        if self._mb_7d and u.get("w") is not None:
            parts.append(f"7d {int(u['w'])}%")
        return " · ".join(parts) if parts else "🐾"

    def _apply_title(self, title):
        """Set the status-item title (MAIN thread only)."""
        try:
            self._item.button().setTitle_(title)
        except Exception:
            pass

    def update_menubar(self):
        """Recompute + apply the status-item title from any thread."""
        if not self._ok:
            return
        title = self._menubar_title()
        try:
            self._AppHelper.callAfter(self._apply_title, title)
        except Exception:
            pass

    # ── face animation (timer runs only while the menu is open) ────────────────
    def _start_anim(self):
        if not self._ok:
            return
        self._stop_anim()
        try:
            n = len(self._frames) if (self._frames and self._frames_state == self._state) else 0
            interval = (2.2 / n) if n > 1 else 0.12   # animate if frames; else just keep panel live
            timer = self._NSTimer.timerWithTimeInterval_target_selector_userInfo_repeats_(
                interval, self._delegate, "animTick:", None, True)
            # Common modes so it keeps ticking during the menu's modal tracking loop.
            self._NSRunLoop.currentRunLoop().addTimer_forMode_(timer, self._NSRunLoopCommonModes)
            self._timer = timer
        except Exception:
            self._timer = None

    def _stop_anim(self):
        if self._timer is not None:
            try:
                self._timer.invalidate()
            except Exception:
                pass
            self._timer = None

    def _anim_tick(self):
        if self._frames and self._frames_state == self._state:
            self._anim_i += 1
        self._refresh()      # advance frame + keep label/device/usage live
