"""HookSocketServer — the ~/.deskbuddy/sock endpoint that Claude Code hooks hit.

Reimplements bridge/deskbuddy.py's `Bridge.serve` so existing hooks (which call
`bridge/deskbuddy event <name>` → `cli_send` → one line over this socket) keep
working unchanged. Runs on the asyncio loop thread; each line is handed to
`on_line` (the controller's arbitration) and `status` replies with the same
string the old daemon produced so `deskbuddy status` still works.
"""

import asyncio
import os

RUN_DIR = os.path.expanduser("~/.deskbuddy")
SOCK_PATH = os.path.join(RUN_DIR, "sock")


class HookSocketServer:
    def __init__(self, on_line, status_line, log):
        self.on_line = on_line                # (line: str) -> None
        self.status_line = status_line        # () -> str
        self.log = log
        self.server = None

    async def start(self):
        os.makedirs(RUN_DIR, exist_ok=True)
        if os.path.exists(SOCK_PATH):
            os.unlink(SOCK_PATH)
        self.server = await asyncio.start_unix_server(self._on_client, path=SOCK_PATH)
        self.log(f"listening on {SOCK_PATH}")

    async def _on_client(self, reader, writer):
        try:
            data = await asyncio.wait_for(reader.readline(), timeout=1.0)
            line = data.decode(errors="replace").strip()
            if line == "status":
                writer.write(self.status_line().encode())
                await writer.drain()
            elif line:
                self.on_line(line)
        except Exception:
            pass
        finally:
            try:
                writer.close()
            except Exception:
                pass
