import subprocess
import time
import json
import os

def main():
    exe_path = os.path.abspath('build/native/Release/OmniBrowser.exe')
    target = r'C:\Users\Nicol\.gemini\antigravity-ide\brain\d2212c22-f2c9-415c-83df-cec16de82c3f\google_find_test.png'
    
    proc = subprocess.Popen([exe_path, '--mcp', '--omni-private'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)

    def send(method, params=None):
        proc.stdin.write(json.dumps({'jsonrpc': '2.0', 'id': 1, 'method': method, 'params': params or {}}) + '\n')
        proc.stdin.flush()
        line = proc.stdout.readline()
        if not line:
            return {}
        return json.loads(line)

    init = send('initialize', {'protocolVersion': '2024-11-05', 'capabilities': {}, 'clientInfo': {'name': 'test', 'version': '1'}})
    print('Init:', init.get('result', {}).get('serverInfo'))
    time.sleep(2)

    # 1. Navigate to Google
    print('Navigating to google.com...')
    send('tools/call', {'name': 'browser_navigate', 'arguments': {'url': 'https://www.google.com'}})
    send('tools/call', {'name': 'browser_wait_for_load', 'arguments': {'timeoutMs': 10000}})
    time.sleep(2)

    # 2. Inject find bar script
    js_code = """
    (() => {
      let host = document.getElementById('omni-find-host');
      if (host) {
        host.remove();
        return;
      }
      host = document.createElement('div');
      host.id = 'omni-find-host';
      host.style.cssText = 'position:fixed;top:16px;right:24px;z-index:2147483647;pointer-events:auto;';
      const shadow = host.attachShadow({mode: 'open'});
      shadow.innerHTML = `
        <style>
          * { box-sizing: border-box; }
          .find-pill {
            display: flex;
            align-items: center;
            gap: 8px;
            padding: 6px 14px;
            background: rgba(26, 25, 26, 0.94);
            backdrop-filter: blur(16px);
            -webkit-backdrop-filter: blur(16px);
            border: 1px solid rgba(255, 255, 255, 0.2);
            border-radius: 24px;
            box-shadow: 0 8px 32px rgba(0, 0, 0, 0.45);
            font-family: 'Segoe UI', system-ui, sans-serif;
            color: #e4e4e4;
            user-select: none;
          }
          .find-icon {
            display: flex;
            align-items: center;
            color: #8a8a8a;
          }
          .find-input {
            width: 170px;
            height: 28px;
            padding: 0 10px;
            background: rgba(255, 255, 255, 0.08);
            border: 1px solid rgba(255, 255, 255, 0.14);
            border-radius: 14px;
            color: #ffffff;
            font-size: 13px;
            outline: none;
          }
          .find-input:focus {
            border-color: #66c0f4;
            box-shadow: 0 0 0 2px rgba(102, 192, 244, 0.25);
          }
          .find-count {
            font-size: 11px;
            color: #8a8a8a;
            padding: 2px 8px;
            border-radius: 10px;
            background: rgba(255, 255, 255, 0.06);
          }
          .find-btn {
            display: flex;
            align-items: center;
            justify-content: center;
            width: 26px;
            height: 26px;
            border-radius: 50%;
            border: none;
            background: transparent;
            color: #8a8a8a;
            cursor: pointer;
            font-size: 11px;
          }
          .find-btn:hover {
            background: rgba(255, 255, 255, 0.12);
            color: #ffffff;
          }
        </style>
        <div class="find-pill">
          <span class="find-icon"><svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/></svg></span>
          <input class="find-input" type="text" placeholder="Find in page..." autofocus />
          <span class="find-count">0/0</span>
          <button class="find-btn" title="Previous (Shift+Enter)">▲</button>
          <button class="find-btn" title="Next (Enter)">▼</button>
          <button class="find-btn" title="Close (Escape)">✕</button>
        </div>
      `;
      (document.body || document.documentElement).appendChild(host);
      const input = shadow.querySelector('.find-input');
      input.focus();
    })();
    """

    res = send('tools/call', {'name': 'browser_eval_js', 'arguments': {'expression': js_code}})
    print('Eval res:', res.get('result', {}).get('isError'))
    time.sleep(2)

    # 3. Take screenshot
    print('Taking screenshot to', target)
    send('tools/call', {'name': 'browser_take_screenshot', 'arguments': {'savePath': target}})
    time.sleep(1)

    if os.path.exists(target):
        print('Screenshot size:', os.path.getsize(target))

    proc.terminate()

if __name__ == '__main__':
    main()
