# OmniBrowser MCP (Model Context Protocol) & ACP Integration

OmniBrowser includes native, built-in support for **Model Context Protocol (MCP)** and **Agent Control Protocol (ACP)**, enabling AI assistants (Claude Desktop, Cursor, Antigravity, Cline, Windsurf, LangChain / AutoGPT / Custom LLM Agents) to interact directly with the browser, inspect tabs, extract structured content, and automate web actions.

---

## 🚀 Key Features

- **Standard MCP 2024-11-05 Specification**:
  - `tools/list` & `tools/call` for full browser automation.
  - `resources/list` & `resources/read` for instant state inspection (`omni://tabs/active`, `omni://tabs`, `omni://history/recent`).
  - `prompts/list` & `prompts/get` for preconfigured browser research & summarization templates.
- **Dual Transports**:
  - **Stdio Transport (MCP standard)**: `OmniBrowser.exe --mcp` is a lightweight JSON-RPC host on stdin/stdout (no CEF in that process). It starts the GUI if needed and proxies tool calls to `http://127.0.0.1:8999/mcp`. Tool failures, timeouts, disconnects, and crashes are returned to the agent as `tools/call` results with `isError: true`.
  - **Local HTTP / SSE Transport**: The running GUI serves `http://127.0.0.1:8999/` with `/sse`, `/message`, and `/mcp`.
- **Agent HUD**: While any agent is controlling the browser, an "Agent Controlled" pill is shown in the main view (over internet pages and native UI). Use **Take Control** to pause agent chrome.
- **No External Puppeteer/Playwright Overhead**: Directly interfaces with OmniBrowser's native CEF (Chromium) engine and tab lifecycle.

---

## 🛠 Available AI Tools

| Tool Name | Description | Arguments |
|---|---|---|
| `browser_navigate` | Navigate active or specified tab to a URL | `url` (string, required), `tabId` (string, optional) |
| `browser_list_tabs` | List all open tabs (id, title, url, active) | None |
| `browser_create_tab` | Open a new tab | `url` (string, optional), `tabId` (string, optional) |
| `browser_activate_tab` | Switch focus to a tab | `tabId` (string, required) |
| `browser_close_tab` | Close a tab | `tabId` (string, required) |
| `browser_reload_tab` | Reload tab | `tabId` (string, optional), `ignoreCache` (boolean, optional) |
| `browser_go_back` | Go back in history | None |
| `browser_go_forward` | Go forward in history | None |
| `browser_extract_content` | Extract capped page text, title, and URL | `tabId` (string, optional) |
| `browser_get_html` | Get size-capped HTML (never a full Wikipedia DOM) | `selector` (string, optional), `tabId` (string, optional) |
| `browser_eval_js` | Evaluate JavaScript via CDP (not blocked by page CSP) | `expression` (string, required), `tabId` (string, optional) |
| `browser_click` | Click DOM element by CSS selector | `selector` (string, required), `tabId` (string, optional) |
| `browser_fill` | Type text into input/textarea | `selector` (string, required), `value` (string, required), `tabId` (optional) |
| `browser_upload_file` | Set files on a file input (no OS dialog) | `files` (string or string[], required), `selector` (optional), `tabId` (optional) |
| `browser_scroll` | Scroll page | `position` ("top" \| "bottom" \| "by"), `y` (int), `tabId` (optional) |
| `browser_get_history` | Query recent navigation history | `limit` (int, optional) |
| `browser_get_bookmarks` | Query bookmarks | None |
| `browser_wait_for_load` | Wait until the tab finishes loading | `tabId` (optional), `timeoutMs` (optional, default 15000) |
| `browser_status` | Browser readiness, active tab, connected agents | None |

---

## ⚙️ Client Configurations

### 1. Claude Desktop

Add OmniBrowser to your `claude_desktop_config.json` (located at `%APPDATA%\Claude\claude_desktop_config.json` on Windows):

#### Option A: Stdio Pipe
```json
{
  "mcpServers": {
    "omni-browser": {
      "command": "C:\\path\\to\\OmniBrowser.exe",
      "args": ["--mcp"]
    }
  }
}
```

#### Option B: SSE Stream (when OmniBrowser is running)
```json
{
  "mcpServers": {
    "omni-browser": {
      "url": "http://127.0.0.1:8999/sse"
    }
  }
}
```

---

### 2. Cursor / Antigravity / Windsurf / Cline

Workspace `.cursor/mcp.json` (this repo already has it):

```json
{
  "mcpServers": {
    "omni-browser": {
      "command": "build/native/Release/OmniBrowser.exe",
      "args": ["--mcp"]
    }
  }
}
```

That is the native MCP server (stdio, MCP 2024-11-05). Build Release first so the exe exists. Reload MCP in Cursor (**Settings → MCP**) after changing this file.

Sub-agents can use the same command, or POST JSON-RPC to `http://127.0.0.1:8999/mcp` while Omni is running. If the GUI crashes mid-call, the stdio host stays up and returns an `isError` result instead of dropping the pipe.

---

### 3. Direct HTTP / REST / Python Agent Access

You can also interact directly with OmniBrowser using any HTTP client:

#### Check Server Status
```bash
curl http://127.0.0.1:8999/
```

#### List Available Tools
```bash
curl -X POST http://127.0.0.1:8999/mcp \
     -H "Content-Type: application/json" \
     -d "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}"
```

#### Navigate Browser
```bash
curl -X POST http://127.0.0.1:8999/mcp \
     -H "Content-Type: application/json" \
     -d "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"browser_navigate\",\"arguments\":{\"url\":\"https://github.com\"}}}"
```

#### Extract Page Content (Markdown & Text)
```bash
curl -X POST http://127.0.0.1:8999/mcp \
     -H "Content-Type: application/json" \
     -d "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"browser_extract_content\",\"arguments\":{}}}"
```

---

## 🚩 Command Line Flags

| Flag | Description |
|---|---|
| `--mcp` / `--mcp-stdio` | Native MCP stdio host (no CEF). Starts the GUI if needed and forwards all tool/browser errors to the agent. |
| `--mcp-port=<port>` | Specify custom port for HTTP/SSE server (Default: `8999`) |
| `--no-mcp` | Disable the background MCP HTTP/SSE server |
