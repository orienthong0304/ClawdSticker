# -*- mode: python ; coding: utf-8 -*-
# PyInstaller spec for ClawdSticker.app (macOS, arm64).
# Build:  cd desktop && pyinstaller packaging/ClawdSticker.spec --noconfirm --clean
# Version is injected via the CLAWDSTICKER_VERSION env var (CI passes the git tag).

import os
from PyInstaller.utils.hooks import collect_submodules

PKG_DIR = os.path.abspath(SPECPATH)            # desktop/packaging
DESKTOP_DIR = os.path.dirname(PKG_DIR)          # desktop
WEB_DIR = os.path.join(DESKTOP_DIR, "web")      # desktop/web  → bundled as <_MEIPASS>/web
VERSION = os.environ.get("CLAWDSTICKER_VERSION", "0.0.0")

# pywebview (Cocoa/WKWebView), bleak (CoreBluetooth) and pyobjc load a lot of
# their guts dynamically — collect them explicitly so the freeze doesn't miss them.
hidden = (
    collect_submodules("webview")
    + collect_submodules("bleak")
    + ["objc", "Foundation", "AppKit", "WebKit", "CoreBluetooth"]
)

a = Analysis(
    [os.path.join(PKG_DIR, "entry.py")],
    pathex=[DESKTOP_DIR],
    binaries=[],
    datas=[(WEB_DIR, "web")],
    hiddenimports=hidden,
    hookspath=[],
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
)

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="ClawdSticker",
    debug=False,
    strip=False,
    upx=False,
    console=False,
    target_arch="arm64",
)

coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=False,
    name="ClawdSticker",
)

app = BUNDLE(
    coll,
    name="ClawdSticker.app",
    icon=os.path.join(PKG_DIR, "AppIcon.icns"),
    bundle_identifier="com.clawdsticker.app",
    version=VERSION,
    info_plist={
        "CFBundleName": "ClawdSticker",
        "CFBundleDisplayName": "ClawdSticker",
        "CFBundleShortVersionString": VERSION,
        "CFBundleVersion": VERSION,
        # Without a Bluetooth usage string the sandboxed/hardened app gets no
        # CoreBluetooth permission and bleak scanning fails at runtime.
        "NSBluetoothAlwaysUsageDescription":
            "ClawdSticker 通过蓝牙连接桌面伴侣设备,实时显示 Claude Code 的运行状态。",
        "LSMinimumSystemVersion": "12.0",
        "NSHighResolutionCapable": True,
        # Menu-bar accessory app: no Dock icon. app.py also flips the activation
        # policy at runtime (pywebview forces Regular at startup).
        "LSUIElement": True,
    },
)
