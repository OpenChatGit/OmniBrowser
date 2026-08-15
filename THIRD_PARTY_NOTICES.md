# Third-party notices

## adblock-rust (Mozilla Public License 2.0)

Omni Browser embeds [Brave’s adblock-rust](https://github.com/brave/adblock-rust) engine
via a local FFI crate (`native/adblock_ffi`) for network blocking, cosmetic filtering,
scriptlets, and redirect resources.

- License: **MPL-2.0**
- Source: https://github.com/brave/adblock-rust
- Modifications to MPL-covered files (if any) are in `native/adblock_ffi/`

You may obtain a copy of the License at https://www.mozilla.org/MPL/2.0/

## Chromium Embedded Framework (CEF) / Chromium

Downloaded by `scripts/download_cef.ps1` into `third_party/cef/` (not vendored in git).
See `third_party/cef/LICENSE.txt` after download.

## SQLite

`third_party/sqlite/` — public domain.

## Filter lists (EasyList / EasyPrivacy / Fanboy)

Downloaded at runtime into `%APPDATA%\OmniBrowser\adblock\`.
These lists have their own licenses (typically Creative Commons or similar);
see headers inside each list file.

## Brave adblock-resources + uBlock Origin scriptlets

Bundled redirect/scriptlet resources under `resources/adblock/resources.json`
combine:

- [brave/adblock-resources](https://github.com/brave/adblock-resources) (MPL-2.0)
- Scriptlets and web-accessible redirect resources from
  [uBlock Origin](https://github.com/gorhill/uBlock) (GPLv3), assembled the same
  way Brave’s adblock component packager does

Regenerate with `build/ubo-resources/generate-resources.mjs` after refreshing the
local uBlock snapshot under `build/ubo-resources/ublock-src/` (build artifact only).
