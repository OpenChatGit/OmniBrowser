# Omni Browser

Desktop browser: **C++** host + **CEF** (Chromium). No Electron, no Tauri. **Windows** and **Linux** (including WSL2 + WSLg).

Default search is **QuBrain Search**. Chrome, tabs, start page, private windows, and library pages (History, Bookmarks, Downloads) live in this repo.

Native **MCP (Model Context Protocol)** and **ACP (Agent Control Protocol)** server support allows AI assistants (Claude, Cursor, Antigravity, custom agents) to directly control tabs, navigate, extract markdown content, and execute browser interactions. See [MCP Integration Documentation](docs/MCP_INTEGRATION.md).

---

## What you need

| Tool | Why |
|------|-----|
| Windows 10/11 **x64**, or **Linux x64** (Ubuntu/Debian or WSL2) | Host platforms |
| [CMake](https://cmake.org/download/) **3.21+** | Configure the build (`cmake` on `PATH`) |
| **Windows:** Visual Studio 2022/2026 with **Desktop development with C++**. **Linux/WSL:** `build-essential` | C++ compiler |
| **Ninja** | Fast generator |
| [Rust](https://rustup.rs/) stable (`cargo` / `rustc` on `PATH`) | Brave **adblock-rust** static library |
| ~**2 GB** free disk | CEF binary download + build |

Check the tools in a **new** PowerShell:

```powershell
cmake --version
rustc --version
cargo --version
```

---

## 1. Get the code

```powershell
git clone https://github.com/OpenChatGit/OmniBrowser.git
cd OmniBrowser
```

Use your clone path. The rest of this file assumes you are in the **repo root**.

---

## 2. Download CEF (once)

CEF is **not** in git. This puts it in `third_party/cef/` (~1–2 GB):

```powershell
powershell -ExecutionPolicy Bypass -File scripts/download_cef.ps1
```

Re-run only if that folder is missing or you wiped `third_party/`.

If CMake later says `CEF not found at .../third_party/cef`, this step was skipped.

---

## 3. Open a Visual Studio x64 developer environment

MSVC and Ninja must be on `PATH`. Easiest: run **x64 Native Tools Command Prompt** from the Start menu.

Or from PowerShell, load `vcvars64.bat` (pick the path that exists on your machine):

```powershell
# Visual Studio 2026
cmd /c "`"%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat`" && set"

# Visual Studio 2022
cmd /c "`"%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat`" && set"
```

Editions can be `Community`, `Professional`, or `Enterprise`. If you are unsure:

```powershell
Get-ChildItem "${env:ProgramFiles}\Microsoft Visual Studio" -Recurse -Filter vcvars64.bat -ErrorAction SilentlyContinue |
  Select-Object -ExpandProperty FullName
```

All **configure** and **build** commands below must run in a shell where `vcvars64` has already been applied. A normal PowerShell without that step will fail with `cl` / `ninja` not found.

---

## 4. Configure (once, or after CMake / CEF changes)

From the **repo root**, in the VS x64 shell:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_SANDBOX=OFF
```

This only generates the Ninja files under `build/`. You do **not** need to repeat it for every code change.

---

## 5. Build

Same shell, repo root:

```bat
cmake --build build --target OmniBrowser --config Release
```

First build takes a while (CEF wrapper + Rust adblock + the app). Later builds only compile what changed.

**If the linker fails with LNK1104** (`OmniBrowser.exe` cannot be opened): the app is still running. Close every Omni window (including private ones) and build again.

After a successful build you get:

```
build\native\Release\OmniBrowser.exe
```

plus CEF DLLs, `locales\`, `ui\`, and `adblock\` in that **same folder**.

---

## 6. Start

**Always start from the Release folder.** Chromium loads `libcef.dll` and pak files from the current working directory. Starting the exe from elsewhere (Explorer shortcut, another `cd`, a copied exe alone) will fail or crash.

```powershell
cd build\native\Release
.\OmniBrowser.exe
```

Full example:

```powershell
cd C:\Users\Nicol\Documents\Github\browser\build\native\Release
.\OmniBrowser.exe
```

Do not copy only `OmniBrowser.exe` to another directory without the accompanying DLLs, assets, and resources. Always run it inside the `Release\` folder.

### Daily loop

| You changed… | What to do |
|--------------|------------|
| `ui/` (HTML / CSS / JS) | Save. No rebuild. Restart, or wait for hot reload (title shows `[dev · hot reload]`) |
| C++ / CMake / Rust adblock | Close the app → `cmake --build build --target OmniBrowser --config Release` → start from `build\native\Release` |
| `CMakeLists.txt` or CEF version | Close the app → configure again (step 4) → build → start |

---

## Building on Linux / WSL

The native host is cross-platform. On Windows, use a real Linux distro in WSL2 (Ubuntu), not the Docker Desktop VM. WSLg is needed to show the window.

From PowerShell:

```powershell
wsl -d Ubuntu -- bash -lc 'cd /mnt/c/Users/Nicol/Documents/Github/browser && bash scripts/setup_wsl.sh && bash scripts/build_linux.sh'
```

Or inside WSL, from the repo root:

```bash
bash scripts/setup_wsl.sh
bash scripts/build_linux.sh
cd build-linux/native/Release   # or build-linux/native/
./OmniBrowser
```

`setup_wsl.sh` installs CMake, Ninja, GTK 3, X11, NSS, and Rust. `build_linux.sh` downloads **linux64** CEF into `third_party/cef-linux64` (kept separate from the Windows CEF tree) and builds with Ninja.

Start from the folder that contains `libcef.so`, the `.pak` files, and `ui/`. Same rule as Windows.

Building on `/mnt/c/...` works but is slower than a clone on the Linux filesystem (`~/src`).

### Dependencies (Ubuntu / Debian)

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake ninja-build pkg-config \
  libgtk-3-dev libnss3-dev libxss-dev libasound2-dev \
  libx11-dev libxcomposite-dev libxdamage-dev libxrandr-dev \
  libgbm-dev libpango1.0-dev libatk1.0-dev libcups2-dev \
  curl tar zenity
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source "$HOME/.cargo/env"
```

CEF Views on Linux needs GTK 3. Other distros: equivalent `-dev` packages for GTK, NSS, X11, GBM, ALSA.

### CEF download

```bash
bash scripts/download_cef.sh
# optional: bash scripts/download_cef.sh "<same version as download_cef.ps1>" linuxarm64
```

### Configure and run

```bash
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_SANDBOX=OFF
cmake --build build-linux --target OmniBrowser
cd build-linux/native/Release
./OmniBrowser
```

Linux-specific pieces: `main_linux.cpp`, POSIX pty terminal, XDG data dir (`~/.local/share/OmniBrowser`), `xdg-open`, zenity file picker. The Win32 AI HUD overlay and native menu hooks stay Windows-only; the HTML/CSS/JS under `ui/` is shared.

---

## UI hot reload

On the machine where you built, Omni loads UI from the repo `ui/` folder and reloads on save.

1. Start `OmniBrowser.exe` from `build\native\Release`.
2. Edit files under `ui/`.
3. Save — the window reloads (title: `[dev · hot reload]`).

| Flag / env | Effect |
|------------|--------|
| (default, source `ui/` present) | Live source UI + hot reload |
| `--dev` / `OMNI_DEV=1` | Force dev mode |
| `--bundled-ui` | Use `ui/` next to the EXE (shipping / CI) |

---

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

---

## Native API (ApiDispatcher)

All `cefQuery` RPCs go through a single registry (`native/src/api/`):

- Transport: [`library_ipc.cpp`](native/src/ipc/library_ipc.cpp) parses JSON → `ApiDispatcher::Dispatch`
- Domains register in [`register_apis.cpp`](native/src/api/register_apis.cpp) (`browser.*`, `window.*`, `browser.adblock.*`, …)
- New features plug into the same table via `Register` / `RegisterPrefix` + optional `ApiExposure::RemoteSafe`

UI helpers stay in [`ui/js/bridge.js`](ui/js/bridge.js); use `OmniBridge.call(method, params)` for one-off methods.

---

## Ad blocking (adblock-rust)

Omni embeds Brave’s open-source **adblock-rust** engine (network + cosmetics, not full Brave Shields):

- Network blocking via CEF `OnBeforeResourceLoad` (content tabs only)
- Cosmetic hide CSS on the main frame (`OnLoadStart` / `OnLoadEnd`)
- Redirect resources (`noop.js`, transparent pixels, Brave resource pack, …)
- Bundled baseline; EasyList / EasyPrivacy in the Omni data dir (`%APPDATA%\OmniBrowser\adblock\` or `~/.local/share/OmniBrowser/adblock/`); Fanboy when aggressive
- Shield in the omnibox; app menu toggles for global / aggressive

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for MPL-2.0 attribution.

### Smoke checklist

1. Build with Rust on `PATH`, run `OmniBrowser.exe` from `build\native\Release`.
2. Visit a page that loads `googlesyndication.com` / similar — requests should be cancelled; shield count rises.
3. Ads/elements should hide early via cosmetic CSS.
4. Toggle **Block ads & trackers** off — ads/trackers load again.
5. Toggle **Aggressive ad blocking** on — Fanboy Annoyance is used after lists are on disk.

---

## Architecture

| Layer | Tech |
|--------|------|
| Host | C++17, CEF (Win32 + Linux/GTK) |
| UI shell | CEF Views + vanilla HTML/CSS/JS |
| IPC | `cefQuery` → **ApiDispatcher** registry |
| Search | Cloudflare Worker (`workers/omni-search`) |
| Ad blocking | Brave adblock-rust (MPL-2.0) via Rust FFI |

---

## License

Omni Browser source in this repository is **MIT** — see [LICENSE](LICENSE).

Third-party components keep their own licenses:

- CEF / Chromium: `third_party/cef/LICENSE.txt` (download via `scripts/download_cef.ps1`; not vendored in git)
- SQLite: public domain
- adblock-rust: MPL-2.0 — see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
