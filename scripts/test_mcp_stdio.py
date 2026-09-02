import subprocess
import json
import os
import sys

def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    exe_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "build", "native", "Release", "OmniBrowser.exe"))
    if not os.path.exists(exe_path):
        print(f"Executable not found at {exe_path}")
        sys.exit(1)

    print(f"Testing MCP stdio with {exe_path}...")
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

    # 1. Test initialize
    init_req = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {"name": "test-client", "version": "1.0.0"}
        }
    }
    proc.stdin.write(json.dumps(init_req) + "\n")
    proc.stdin.flush()

    line = proc.stdout.readline()
    print("1. Initialize response:", line.strip())
    assert "protocolVersion" in line, "Failed initialize"

    # 2. Test tools/list
    tools_req = {
        "jsonrpc": "2.0",
        "id": 2,
        "method": "tools/list",
        "params": {}
    }
    proc.stdin.write(json.dumps(tools_req) + "\n")
    proc.stdin.flush()

    line = proc.stdout.readline()
    print("2. Tools list response:", line[:150] + "...")
    assert "browser_navigate" in line, "Failed tools/list"

    # 3. Test resources/list
    res_req = {
        "jsonrpc": "2.0",
        "id": 3,
        "method": "resources/list",
        "params": {}
    }
    proc.stdin.write(json.dumps(res_req) + "\n")
    proc.stdin.flush()

    line = proc.stdout.readline()
    print("3. Resources list response:", line.strip())
    assert "omni://tabs" in line, "Failed resources/list"

    # 4. Test prompts/list
    prompt_req = {
        "jsonrpc": "2.0",
        "id": 4,
        "method": "prompts/list",
        "params": {}
    }
    proc.stdin.write(json.dumps(prompt_req) + "\n")
    proc.stdin.flush()

    line = proc.stdout.readline()
    print("4. Prompts list response:", line.strip())
    assert "summarize_page" in line, "Failed prompts/list"

    # 5. Test browser_list_tabs call
    call_req = {
        "jsonrpc": "2.0",
        "id": 5,
        "method": "tools/call",
        "params": {
            "name": "browser_list_tabs",
            "arguments": {}
        }
    }
    proc.stdin.write(json.dumps(call_req) + "\n")
    proc.stdin.flush()

    line = proc.stdout.readline()
    print("5. Call browser_list_tabs response:", line.strip())

    # 6. Test create tab & navigate
    nav_req = {
        "jsonrpc": "2.0",
        "id": 6,
        "method": "tools/call",
        "params": {
            "name": "browser_create_tab",
            "arguments": {"url": "https://example.com"}
        }
    }
    proc.stdin.write(json.dumps(nav_req) + "\n")
    proc.stdin.flush()

    line = proc.stdout.readline()
    print("6. Call browser_create_tab response:", line.strip())
    assert "tabId" in line, "Failed browser_create_tab"

    # 7. Test history
    hist_req = {
        "jsonrpc": "2.0",
        "id": 7,
        "method": "tools/call",
        "params": {
            "name": "browser_get_history",
            "arguments": {}
        }
    }
    proc.stdin.write(json.dumps(hist_req) + "\n")
    proc.stdin.flush()

    line = proc.stdout.readline()
    print("7. Call browser_get_history response:", line.strip())

    proc.terminate()
    print("\n[SUCCESS] All MCP protocol and browser tool tests completed successfully!")

if __name__ == "__main__":
    main()
