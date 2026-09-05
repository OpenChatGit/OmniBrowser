import subprocess
import json
import os
import sys
import time

def main():
    exe_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "build", "native", "Release", "OmniBrowser.exe"))
    target_screenshot = r"C:\Users\Nicol\.gemini\antigravity-ide\brain\d2212c22-f2c9-415c-83df-cec16de82c3f\find_overlay_screenshot.png"
    
    print(f"Connecting to OmniBrowser MCP at {exe_path}...")
    proc = subprocess.Popen(
        [exe_path, "--mcp", "--omni-private"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=1
    )

    req_id = 1
    def send(method, params=None):
        nonlocal req_id
        payload = {
            "jsonrpc": "2.0",
            "id": req_id,
            "method": method,
            "params": params or {}
        }
        req_id += 1
        proc.stdin.write(json.dumps(payload) + "\n")
        proc.stdin.flush()
        line = proc.stdout.readline()
        if not line:
            return None
        return json.loads(line)

    # 1. Initialize
    init = send("initialize", {
        "protocolVersion": "2024-11-05",
        "capabilities": {},
        "clientInfo": {"name": "test-find-overlay", "version": "1.0.0"}
    })
    print("Initialize:", init.get("result", {}).get("serverInfo"))

    time.sleep(2)

    # 2. Navigate to Wikipedia
    nav_url = "https://en.wikipedia.org/wiki/Antigravity"
    print(f"Navigating to {nav_url}...")
    send("tools/call", {
        "name": "browser_navigate",
        "arguments": {"url": nav_url}
    })

    # 3. Wait for load
    send("tools/call", {
        "name": "browser_wait_for_load",
        "arguments": {"timeoutMs": 10000}
    })
    time.sleep(2)

    # 4. Trigger Find Bar overlay natively via MCP
    print("Calling browser_toggle_find to open Find Bar overlay...")
    toggle_res = send("tools/call", {
        "name": "browser_toggle_find",
        "arguments": {}
    })
    print("Toggle result:", toggle_res.get("result", {}))

    time.sleep(2)

    # 5. Take screenshot to visually verify find overlay sits atop the main content
    print(f"Capturing screenshot to {target_screenshot}...")
    ss_res = send("tools/call", {
        "name": "browser_take_screenshot",
        "arguments": {
            "savePath": target_screenshot,
            "format": "png"
        }
    })
    print("Screenshot response ok:", ss_res.get("result", {}).get("isError") is not True)

    if os.path.exists(target_screenshot):
        print(f"[SUCCESS] Screenshot created: {target_screenshot} ({os.path.getsize(target_screenshot)} bytes)")

    proc.terminate()
    try:
        proc.wait(timeout=3)
    except Exception:
        proc.kill()

if __name__ == "__main__":
    main()
