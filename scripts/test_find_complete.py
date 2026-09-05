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
        "clientInfo": {"name": "test-find-complete", "version": "1.0.0"}
    })
    print("Initialize:", init.get("result", {}).get("serverInfo"))
    time.sleep(3)

    # 2. Wait for page load
    send("tools/call", {
        "name": "browser_wait_for_load",
        "arguments": {"timeoutMs": 10000}
    })
    time.sleep(1)

    # 3. Test AI Tool `browser_find` WITHOUT opening search bar!
    print("--- Testing AI Tool browser_find ---")
    ai_find_res = send("tools/call", {
        "name": "browser_find",
        "arguments": {
            "query": "Google",
            "highlight": True,
            "maxMatches": 5
        }
    })
    print("browser_find result:", json.dumps(ai_find_res, indent=2))
    assert ai_find_res.get("result", {}).get("isError") is not True, "browser_find failed"

    # 4. Open UI Find Bar
    print("--- Opening Find Bar Overlay ---")
    send("tools/call", {"name": "browser_toggle_find", "arguments": {}})
    time.sleep(1)

    # 5. Type 'Google' into the in-page search bar and verify focus & highlight
    print("--- Simulating typing in Find Bar ---")
    type_res = send("tools/call", {
        "name": "browser_eval_js",
        "arguments": {
            "expression": """
            (() => {
                const host = document.getElementById('omni-find-host');
                if (!host || !host.shadowRoot) return { error: 'no host' };
                const input = host.shadowRoot.querySelector('.find-input');
                const count = host.shadowRoot.querySelector('.find-count');
                const icon = host.shadowRoot.querySelector('.find-icon');
                
                // Simulate typing 'G', then 'o', then 'o', then 'g', then 'l', then 'e'
                let text = '';
                for (const char of 'Google') {
                    text += char;
                    input.value = text;
                    input.dispatchEvent(new Event('input', { bubbles: true }));
                }
                
                const marks = document.querySelectorAll('mark.omni-match');
                const activeMark = document.querySelector('mark.omni-match[style*="rgb(255, 152, 0)"]') ||
                                   document.querySelector('mark.omni-match[style*="#ff9800"]');
                
                return {
                    iconRemoved: icon === null,
                    inputText: input.value,
                    countText: count.textContent,
                    marksCount: marks.length,
                    activeMarkFound: activeMark !== null,
                    inputFocused: host.shadowRoot.activeElement === input || document.activeElement === host
                };
            })()
            """
        }
    })
    print("Typing evaluation result:", json.dumps(type_res, indent=2))

    time.sleep(1)

    # 6. Capture screenshot to verify compact find bar with active highlights
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
