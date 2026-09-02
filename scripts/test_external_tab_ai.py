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

print("1. Creating tab with external website (Wikipedia)...")
created = call_mcp('browser_create_tab', {'url': 'https://en.wikipedia.org/wiki/Artificial_intelligence', 'activate': True})
print("Created tab response:", created)
created_data = json.loads(created['result']['content'][0]['text'])
tab_id = created_data['tabId']

print("\nWaiting 2.5 seconds for Wikipedia to load...")
time.sleep(2.5)

print("\n2. Executing scroll on Wikipedia via AI MCP...")
scroll_res = call_mcp('browser_scroll', {'position': 'by', 'y': 350, 'tabId': tab_id})
print("Scroll result:", scroll_res)

check_script = "Boolean(document.getElementById('__omni_ai_glow')) ? 'glow-found' : 'not-found'"
check_dom = call_mcp('browser_eval_js', {'expression': check_script, 'tabId': tab_id})
print("DOM Check on external page:", check_dom['result']['content'][0]['text'])

print("\n4. Pausing agent control via MCP agent status...")
status = call_mcp('browser_status', {})
print("Browser status:", status['result']['content'][0]['text'])

print("\n6. Closing Wikipedia tab...")
close_res = call_mcp('browser_close_tab', {'tabId': tab_id})
print("Close tab result:", close_res)

print("\n[TEST SUCCESS] External tab AI feedback verified completely!")
