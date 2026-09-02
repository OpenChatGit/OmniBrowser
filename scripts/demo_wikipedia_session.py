import urllib.request
import json
import time

def call_mcp(name, args=None):
    if args is None:
        args = {}
    req = json.dumps({
        'jsonrpc': '2.0',
        'id': 1,
        'method': 'tools/call',
        'params': {'name': name, 'arguments': args}
    }).encode('utf-8')
    r = urllib.request.urlopen(
        urllib.request.Request(
            'http://127.0.0.1:8999/mcp',
            data=req,
            headers={'Content-Type': 'application/json'}
        )
    )
    return json.loads(r.read().decode('utf-8'))

def main():
    print("[1/7] Navigating to Wikipedia: James Webb Space Telescope...")
    # List tabs or create a fresh one if none active
    tabs_res = call_mcp('browser_list_tabs')
    print("Active tabs:", tabs_res)
    
    nav_res = call_mcp('browser_navigate', {
        'url': 'https://en.wikipedia.org/wiki/James_Webb_Space_Telescope'
    })
    print("Navigated:", nav_res)
    time.sleep(3.5)

    print("\n[2/7] Scrolling smoothly down to introduction overview...")
    for i in range(4):
        call_mcp('browser_scroll', {'position': 'by', 'y': 320})
        time.sleep(1.8)

    print("\n[3/7] Reading Mission overview and mission goals...")
    time.sleep(2.0)
    for i in range(4):
        call_mcp('browser_scroll', {'position': 'by', 'y': 380})
        time.sleep(1.8)

    print("\n[4/7] Scrolling through Scientific Instruments & Optical design...")
    for i in range(5):
        call_mcp('browser_scroll', {'position': 'by', 'y': 420})
        time.sleep(1.8)

    print("\n[5/7] Examining Images & Deep Field discoveries...")
    for i in range(4):
        call_mcp('browser_scroll', {'position': 'by', 'y': 450})
        time.sleep(1.8)

    print("\n[6/7] Scrolling back towards summary highlights...")
    for i in range(3):
        call_mcp('browser_scroll', {'position': 'by', 'y': -350})
        time.sleep(1.5)

    print("\n[7/7] Session preview completed successfully!")

if __name__ == "__main__":
    main()
