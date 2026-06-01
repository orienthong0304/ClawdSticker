#!/bin/bash
# Take a screenshot from the Waveshare AMOLED display via the firmware's
# `screenshot` serial command (dumps the LVGL framebuffer over USB CDC).
#
# Usage: ./screenshot.sh [output.png] [port]
# Default port: first /dev/cu.usbmodem* on macOS, /dev/ttyACM0 on Linux.
#
# AMOLED-1.8 note: this board's USB-Serial-JTAG resets the chip whenever the
# port is opened, and host→device bytes are only consumed once loop() runs.
# So we pulse-reset, wait for the "Dashboard ready" banner, then send the
# command. We also scan the raw byte stream for the START marker (the payload
# is binary and would break line-based parsing).

OUTPUT="${1:-screenshot.png}"
if [ -n "$2" ]; then
    PORT="$2"
else
    case "$(uname -s)" in
        Darwin) PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)" ;;
        *)      PORT="/dev/ttyACM0" ;;
    esac
fi
if [ -z "$PORT" ]; then echo "No serial port found"; exit 1; fi

PY="python3"
if ! python3 -c "import serial" 2>/dev/null; then
    if [ -x "$HOME/.platformio/penv/bin/python" ]; then
        PY="$HOME/.platformio/penv/bin/python"
    fi
fi

TMPRAW="/tmp/screenshot_$$.raw"
TMPDIMS="/tmp/screenshot_$$.dims"
trap "rm -f '$TMPRAW' '$TMPDIMS'" EXIT

echo "Taking screenshot from $PORT..."

"$PY" - "$PORT" "$TMPRAW" "$TMPDIMS" << 'PYEOF'
import serial, time, sys
port_path, raw_path, dims_path = sys.argv[1], sys.argv[2], sys.argv[3]

p = serial.Serial(port_path, 115200, timeout=2)
# Pulse reset via RTS (drives chip EN on the USB-Serial-JTAG), keep DTR
# asserted so the device accepts host data once it is running.
p.setDTR(False); p.setRTS(True); time.sleep(0.2); p.setRTS(False); p.setDTR(True)

# Wait for the firmware to finish booting before sending a command.
t0 = time.time()
while time.time() - t0 < 9:
    line = p.readline().decode("utf-8", "replace").strip()
    if "Dashboard ready" in line:
        break
time.sleep(0.5)
p.reset_input_buffer()
p.timeout = 20
p.write(b"screenshot\n"); p.flush()

# Scan the raw stream for the START marker (payload is binary).
buf = b""; w = h = rs = 0; data = b""
t0 = time.time()
while time.time() - t0 < 18:
    chunk = p.read(512)
    if chunk:
        buf += chunk
    i = buf.find(b"SCREENSHOT_START")
    if i >= 0:
        nl = buf.find(b"\n", i)
        if nl >= 0:
            hdr = buf[i:nl].decode("ascii", "replace").split()
            w, h, rs = int(hdr[1]), int(hdr[2]), int(hdr[3])
            data = buf[nl + 1:]
            break
    if b"SCREENSHOT_ERR" in buf:
        print("Device reported screenshot error", file=sys.stderr); sys.exit(1)
if not rs:
    print(f"No SCREENSHOT_START (got {len(buf)} bytes)", file=sys.stderr); sys.exit(2)

while len(data) < rs:
    chunk = p.read(min(4096, rs - len(data)))
    if not chunk:
        print(f"Timeout: got {len(data)} of {rs} bytes", file=sys.stderr); sys.exit(3)
    data += chunk

with open(raw_path, "wb") as f:
    f.write(data[:rs])
with open(dims_path, "w") as f:
    f.write(f"{w}x{h}")
p.close()
print(f"Captured {w}x{h} ({rs} bytes)")
PYEOF

if [ $? -ne 0 ]; then
    echo "Screenshot capture failed"
    exit 1
fi

DIMS=$(cat "$TMPDIMS")
ffmpeg -y -f rawvideo -pixel_format rgb565le -video_size "$DIMS" \
    -i "$TMPRAW" -update 1 -frames:v 1 "$OUTPUT" 2>/dev/null || true

if [ -f "$OUTPUT" ]; then
    echo "Saved: $OUTPUT ($DIMS)"
else
    echo "Error: conversion failed"
    exit 1
fi
