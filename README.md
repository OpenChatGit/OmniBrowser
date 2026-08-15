# Omni Browser

Open-source desktop browser for Windows: **C++** host + **CEF** (Chromium) UI. No Electron, no Tauri.

Default search is **QuBrain Search** (`api.qubrain.org` / `search.qubrain.org`). The chrome, tabs, start page, and library UI ship from this repo.

## Requirements

- Windows 10/11 x64
- CMake 3.21+
- Visual Studio 2022+ with C++ workload
- **Rust** (stable `cargo` / `rustc` on `PATH`) for Brave **adblock-rust**
- ~2 GB free disk for CEF

## Setup

```powershell
powershell -ExecutionPolicy Bypass -File scripts/download_cef.ps1

cmd /c "`"%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat`" && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_SANDBOX=OFF && cmake --build build --target OmniBrowser"
```

Run:

```powershell
.\build\native\Release\OmniBrowser.exe
```

## UI hot reload (no rebuild)

On the machine where you built the app, Omni Browser loads UI from the **repo `ui/` folder** and watches it for changes.

1. Start the app once (native binary).
2. Edit files under `ui/` (HTML / CSS / JS).
3. Save — the window reloads automatically (title shows `[dev · hot reload]`).

Only rebuild when you change **C++ / CMake**. UI iteration does not need a rebuild.

| Flag / env | Effect |
|------------|--------|
| (default, source `ui/` present) | Live source UI + hot reload |
| `--dev` / `OMNI_DEV=1` | Force dev mode |
| `--bundled-ui` | Use `ui/` next to the EXE (shipping / CI) |

## Project layout

```
native/                 # CEF host, paths, downloads, adblock FFI
native/src/api/         # Central ApiDispatcher + domain handlers
native/adblock_ffi/     # Rust staticlib wrapping Brave adblock-rust
resources/adblock/      # Bundled baseline lists + redirect resources
ui/                     # Start page, SERP, library pages, overlays
workers/omni-search/    # QuBrain Search API + search.qubrain.org
scripts/                # CEF download and helpers
```

## Native API (ApiDispatcher)

All `cefQuery` RPCs go through a single registry (`native/src/api/`):

- Transport: [`library_ipc.cpp`](native/src/ipc/library_ipc.cpp) parses JSON → `ApiDispatcher::Dispatch`
- Domains register in [`register_apis.cpp`](native/src/api/register_apis.cpp) (`browser.*`, `window.*`, `browser.adblock.*`, …)
- New features (e.g. remote Cloudflare adapters later) plug into the same table via `Register` / `RegisterPrefix` + optional `ApiExposure::RemoteSafe`

UI helpers stay in [`ui/js/bridge.js`](ui/js/bridge.js); use `OmniBridge.call(method, params)` for one-off methods.

## Ad blocking (adblock-rust)

Omni Browser embeds Brave’s open-source **adblock-rust** engine (network + cosmetics path, not full Brave Shields):

- Network blocking via CEF `OnBeforeResourceLoad` (content tabs only; IO-safe browser IDs)
- Cosmetic hide CSS on the main frame (`OnLoadStart` / `OnLoadEnd`); scriptlets are prepared but not injected in the page world yet
- Redirect resources (`noop.js`, transparent pixels, Brave resource pack, …)
- Bundled baseline; EasyList / EasyPrivacy in `%APPDATA%\OmniBrowser\adblock\`; Fanboy when aggressive
- Shield UI in the omnibox; app menu toggles for global / aggressive

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for MPL-2.0 attribution.

### Smoke checklist

1. Build with Rust on `PATH`, run `OmniBrowser.exe`.
2. Visit a page that loads `googlesyndication.com` / similar — requests should be cancelled; shield count rises.
3. Ads/elements should hide early (less flash) via cosmetic CSS.
4. Toggle **Block ads & trackers** off — ads/trackers load again.
5. Toggle **Aggressive ad blocking** on — Fanboy Annoyance list is used after lists are on disk.

## Architecture

| Layer | Tech |
|--------|------|
| Host | C++17, Win32, CEF |
| UI shell | CEF Views + vanilla HTML/CSS/JS |
| IPC | `cefQuery` → **ApiDispatcher** registry |
| Search | Cloudflare Worker (`workers/omni-search`) |
| Ad blocking | Brave adblock-rust (MPL-2.0) via Rust FFI |

## License

Omni Browser source in this repository is **MIT** — see [LICENSE](LICENSE).

Third-party components keep their own licenses:

- CEF / Chromium: `third_party/cef/LICENSE.txt` (download via `scripts/download_cef.ps1`; not vendored in git)
- SQLite: public domain
- adblock-rust: MPL-2.0 — see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
