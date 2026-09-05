import subprocess
import json
import os
import sys
import time

def test_screenshot():
    exe_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "build", "native", "Release", "OmniBrowser.exe"))
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

    def send_req(req):
        proc.stdin.write(json.dumps(req) + "\n")
        proc.stdin.flush()
        line = proc.stdout.readline()
        return json.loads(line)

    init_res = send_req({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {"name": "test-client", "version": "1.0.0"}
        }
    })
    print("Init:", init_res.get("result", {}).get("serverInfo"))

    tools_res = send_req({
        "jsonrpc": "2.0",
        "id": 2,
        "method": "tools/list",
        "params": {}
    })
    tool_names = [t["name"] for t in tools_res.get("result", {}).get("tools", [])]
    print("Available tools:", tool_names)
    assert "browser_take_screenshot" in tool_names, "Missing browser_take_screenshot"
    assert "browser_screenshot" in tool_names, "Missing browser_screenshot"

    # Wait a moment for window to create
    time.sleep(2)

    # Call screenshot tool
    test_file = os.path.abspath("test_screenshot.png")
    if os.path.exists(test_file):
        os.remove(test_file)

    call_res = send_req({
        "jsonrpc": "2.0",
        "id": 3,
        "method": "tools/call",
        "params": {
            "name": "browser_take_screenshot",
            "arguments": {
                "filepath": test_file,
                "format": "png"
            }
        }
    })
    print("Screenshot response content types:", [c.get("type") for c in call_res.get("result", {}).get("content", [])])
    has_image = any(c.get("type") == "image" for c in call_res.get("result", {}).get("content", []))
    print("Has image block:", has_image)
    if os.path.exists(test_file):
        print(f"Saved screenshot to disk: {test_file}, size: {os.path.getsize(test_file)} bytes")
        os.remove(test_file)

    # Terminate process
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except Exception:
        proc.kill()

    print("[SUCCESS] Screenshot test completed successfully!")

if __name__ == "__main__":
    test_screenshot()
