# Changelog

All notable changes to OmniBrowser will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.2] - 2026-10-09

### Added
- Added a Windows update installer that downloads the GitHub release ZIP, verifies its SHA-256 checksum, installs it, and restarts OmniBrowser.
- Added a SHA-256 checksum asset to each automated GitHub release.

### Removed
- Removed the QuBrain Search SERP, its SearXNG-backed Cloudflare Worker, map-search integration, and SearXNG deployment configuration. Address-bar searches now use the selected external search engine.

## [0.1.1] - 2026-10-09

### Fixed
- Prevented YouTube ad response filtering from running on allowlisted sites and expanded it to current player, playlist, and Shorts endpoints.
- Kept the risky generic filter-list scriptlets disabled on YouTube while using the dedicated player ad hook.
- Rendered the update panel in the native overlay so it stays above the webpage view.
- Fixed several browser, agent-control, and browsing-data actions, and removed unused assets.

## [0.1.0] - 2026-09-04

### Added
- **Model Context Protocol (MCP) Integration**:
  - Full implementation of 16 autonomous browser control tools over stdio and HTTP/SSE (`http://127.0.0.1:8999/mcp`).
  - Tools for tab lifecycle (`create_tab`, `list_tabs`, `activate_tab`, `close_tab`), navigation (`navigate`, `back`, `forward`, `reload`), DOM queries (`get_dom`, `query_selector`), mouse & keyboard interactions (`click`, `type`, `scroll`), screenshots, and JavaScript evaluation.
  - UI synchronization with real-time tab updates, luminous frame glow, and floating "Agent Controlled" pill with instant "Take Control" user override.
- **Update Notification System**:
  - Dynamic update badge in the topbar beside the main menu with a pulsating status indicator.
  - Interactive update flyout displaying version tag, release notes summary, and direct download links.
  - Automated background checks against GitHub Releases with SemVer comparison.
- **Automated CI/CD Release Pipeline**:
  - GitHub Actions workflow for building Windows x64 binaries, bundling CEF and UI assets, and publishing releases automatically on version tags.
- **Adblock & Privacy Engine**:
  - Rust-based native adblock engine (`adblock_ffi`) supporting EasyList filter sets.
  - Privacy mode and live blocked request counters.
- **Modern UI & Theming**:
  - Chromium Views app menu with transparent item backgrounds matching container color in both Light and Dark themes.
  - Inward submenu alignment ensuring dropdowns stay within window boundaries.
  - View recycling pool (`recycled_content_views_`) preventing CEF crashes during tab lifecycle operations.
