import subprocess
import json
import os
import sys
import time

def main():
    exe_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "build", "native", "Release", "OmniBrowser.exe"))
    target_screenshot = r"C:\Users\Nicol\.gemini\antigravity-ide\brain\d2212c22-f2c9-415c-83df-cec16de82c3f\wiki_screenshot.png"
    
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
        "clientInfo": {"name": "antigravity-agent", "version": "1.0.0"}
    })
    print("Initialize response:", init.get("result", {}).get("serverInfo"))

    time.sleep(2)

    # 2. Navigate to Wikipedia
    nav_url = "https://en.wikipedia.org/wiki/Antigravity"
    print(f"Navigating to {nav_url}...")
    nav_res = send("tools/call", {
        "name": "browser_navigate",
        "arguments": {"url": nav_url}
    })
    print("Navigate result:", nav_res.get("result", {}))

    # 3. Wait for page load
    print("Waiting for page load...")
    wait_res = send("tools/call", {
        "name": "browser_wait_for_load",
        "arguments": {"timeoutMs": 10000}
    })
    print("Wait result:", wait_res.get("result", {}))

    # Give it a moment to render visually
    time.sleep(3)

    # 4. Take screenshot
    print(f"Taking screenshot to {target_screenshot}...")
    ss_res = send("tools/call", {
        "name": "browser_take_screenshot",
        "arguments": {
            "savePath": target_screenshot,
            "format": "png"
        }
    })
    
    result = ss_res.get("result", {})
    content = result.get("content", [])
    print("Screenshot response content items:", len(content))
    for item in content:
        if item.get("type") == "text":
            print("Text info:", item.get("text"))
        elif item.get("type") == "image":
            data_len = len(item.get("data", ""))
            print(f"Image block: {item.get('mimeType')}, {data_len} base64 chars")

    if os.path.exists(target_screenshot):
        size = os.path.getsize(target_screenshot)
        print(f"[SUCCESS] Screenshot created on disk: {target_screenshot} ({size} bytes)")
    else:
        print(f"[WARNING] Screenshot not found on disk at {target_screenshot}")

    # Shutdown
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except Exception:
        proc.kill()

if __name__ == "__main__":
    main()
