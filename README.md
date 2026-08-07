# Omni Browser

Open-source desktop browser for Windows: **C++** host + **CEF** (Chromium) UI. No Electron, no Tauri.

Default search is **QuBrain Search** (`api.qubrain.org` / `search.qubrain.org`). The chrome, tabs, start page, and library UI ship from this repo.

## Requirements

- Windows 10/11 x64
- CMake 3.21+
- Visual Studio 2022+ with C++ workload
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
native/                 # CEF host, IPC, paths, downloads
ui/                     # Start page, SERP, library pages, overlays
workers/omni-search/    # QuBrain Search API + search.qubrain.org
scripts/                # CEF download and helpers
```

## Architecture

| Layer | Tech |
|--------|------|
| Host | C++17, Win32, CEF |
| UI shell | CEF Views + vanilla HTML/CSS/JS |
| IPC | `cefQuery` ↔ `CefMessageRouter` |
| Search | Cloudflare Worker (`workers/omni-search`) |

## License

Omni Browser source in this repository is **MIT** — see [LICENSE](LICENSE).

Third-party components keep their own licenses:

- CEF / Chromium: `third_party/cef/LICENSE.txt` (download via `scripts/download_cef.ps1`; not vendored in git)
- SQLite: public domain
