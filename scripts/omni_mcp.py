#!/usr/bin/env python3
"""Cursor-friendly OmniBrowser MCP stdio adapter.

Cursor spawns this process (not OmniBrowser.exe). The adapter answers
initialize immediately, starts OmniBrowser if needed, and proxies JSON-RPC
to the native HTTP MCP server at 127.0.0.1:8999.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

DEFAULT_PORT = int(os.environ.get("OMNI_MCP_PORT", "8999"))
AGENT_ID = f"cursor-{os.getpid()}"
REPO_ROOT = Path(__file__).resolve().parent.parent


def log(msg: str) -> None:
    sys.stderr.write(f"[omni-mcp] {msg}\n")
    sys.stderr.flush()


def find_exe() -> Path | None:
    env = os.environ.get("OMNI_BROWSER_EXE", "").strip()
    candidates = []
    if env:
        candidates.append(Path(env))
    candidates.extend(
        [
            REPO_ROOT / "build" / "native" / "Release" / "OmniBrowser.exe",
            REPO_ROOT / "build" / "native" / "Debug" / "OmniBrowser.exe",
            Path(sys.executable).resolve().parent / "OmniBrowser.exe",
        ]
    )
    for path in candidates:
        if path.is_file():
            return path
    return None


def mcp_url() -> str:
    return f"http://127.0.0.1:{DEFAULT_PORT}/mcp"


def http_alive(timeout: float = 0.4) -> bool:
    try:
        urllib.request.urlopen(
            urllib.request.Request(f"http://127.0.0.1:{DEFAULT_PORT}/", method="GET"),
            timeout=timeout,
        )
        return True
    except Exception:
        return False


_browser_launched = False


def start_browser() -> None:
    global _browser_launched
    if _browser_launched:
        return
    exe = find_exe()
    if exe is None:
        raise FileNotFoundError(
            "OmniBrowser.exe not found. Build it first "
            "(cmake --build build --target OmniBrowser --config Release) "
            "or set OMNI_BROWSER_EXE."
        )
    flags = 0
    if os.name == "nt":
        flags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP
    log(f"starting {exe}")
    subprocess.Popen(
        [str(exe)],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        close_fds=True,
        cwd=str(exe.parent),
        creationflags=flags,
    )
    _browser_launched = True


def ensure_browser(timeout_sec: float = 25.0) -> None:
    if http_alive():
        return
    start_browser()
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        if http_alive(timeout=0.5):
            return
        time.sleep(0.25)
    raise TimeoutError(
        f"OmniBrowser MCP did not come up on 127.0.0.1:{DEFAULT_PORT} within {int(timeout_sec)}s"
    )


def http_rpc(raw: str, timeout: float = 30.0) -> str:
    req = urllib.request.Request(
        mcp_url(),
        data=raw.encode("utf-8"),
        method="POST",
        headers={
            "Content-Type": "application/json",
            "X-Omni-Agent": AGENT_ID,
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.read().decode("utf-8", errors="replace").strip()
    except urllib.error.HTTPError as err:
        body = err.read().decode("utf-8", errors="replace")
        return body.strip() if body else ""


def local_initialize(req: dict) -> dict:
    return {
        "jsonrpc": "2.0",
        "id": req.get("id"),
        "result": {
            "protocolVersion": "2024-11-05",
            "capabilities": {
                "tools": {},
                "resources": {},
                "prompts": {},
            },
            "serverInfo": {"name": "OmniBrowser-MCP", "version": "1.0.0"},
            "agentId": AGENT_ID,
        },
    }


def write_response(payload: dict | str) -> None:
    if isinstance(payload, dict):
        line = json.dumps(payload, ensure_ascii=False)
    else:
        line = payload.strip()
        if not line:
            return
    sys.stdout.write(line + "\n")
    sys.stdout.flush()


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stdin.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")

    for raw in sys.stdin:
        line = raw.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except json.JSONDecodeError:
            write_response(
                {
                    "jsonrpc": "2.0",
                    "id": None,
                    "error": {"code": -32700, "message": "Parse error"},
                }
            )
            continue

        method = req.get("method", "")
        req_id = req.get("id", None)

        if method == "initialize":
            write_response(local_initialize(req))
            continue

        if method in ("notifications/initialized", "notifications/cancelled"):
            continue

        if method in (
            "ping",
            "logging/setLevel",
            "tools/list",
            "resources/list",
            "prompts/list",
            "prompts/get",
        ):
            if not http_alive():
                if req_id is None:
                    continue
                write_response(
                    {
                        "jsonrpc": "2.0",
                        "id": req_id,
                        "error": {
                            "code": -32000,
                            "message": "OmniBrowser is not running",
                        },
                    }
                )
                continue

        try:
            if method == "tools/call":
                ensure_browser()
            elif not http_alive():
                raise TimeoutError(
                    "OmniBrowser is not running. Start the app yourself, "
                    "or have an agent call a browser tool."
                )
            resp = http_rpc(line)
            if req_id is None:
                continue
            if resp:
                write_response(resp)
            else:
                write_response(
                    {
                        "jsonrpc": "2.0",
                        "id": req_id,
                        "error": {"code": -32000, "message": "Empty MCP HTTP response"},
                    }
                )
        except Exception as err:
            if req_id is None:
                log(str(err))
                continue
            write_response(
                {
                    "jsonrpc": "2.0",
                    "id": req_id,
                    "error": {"code": -32000, "message": str(err)},
                }
            )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(0)
