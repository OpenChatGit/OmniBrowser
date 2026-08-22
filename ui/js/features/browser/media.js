(() => {
  const MEDIA_WIDTH = 320;
  const sessions = new Map();
  let open = false;
  let primaryTabId = null;
  let lastAnchor = null;
  let isStartScrubbing = false;

  const wrap = document.getElementById("browser-media-wrap");
  const btn = document.getElementById("browser-media");

  // Home tab / Start page media subbar elements
  const startWrap = document.getElementById("browser-start-search-wrap");
  const startForm = document.getElementById("browser-start-search-form");
  const startBar = document.getElementById("browser-start-media-bar");
  const startArtWrap = document.getElementById("browser-start-media-art-wrap");
  const startArt = document.getElementById("browser-start-media-art");
  const startFallback = document.getElementById("browser-start-media-art-fallback");
  const startInfo = document.getElementById("browser-start-media-info");
  const startTitle = document.getElementById("browser-start-media-title");
  const startArtist = document.getElementById("browser-start-media-artist");
  const startPlayBtn = document.getElementById("browser-start-media-play");

  const ICON_BACK_10 =
    '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.85" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M3 12a9 9 0 1 0 3-6.7"/><path d="M3 4v5h5"/><text x="12" y="15.5" text-anchor="middle" fill="currentColor" stroke="none" font-size="7.5" font-family="Segoe UI, sans-serif" font-weight="650">10</text></svg>';
  const ICON_FWD_10 =
    '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.85" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M21 12a9 9 0 1 1-3-6.7"/><path d="M21 4v5h-5"/><text x="12" y="15.5" text-anchor="middle" fill="currentColor" stroke="none" font-size="7.5" font-family="Segoe UI, sans-serif" font-weight="650">10</text></svg>';
  const ICON_PLAY =
    '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><polygon points="6 3 20 12 6 21 6 3"/></svg>';
  const ICON_PAUSE =
    '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="14" y="4" width="4" height="16" rx="1"/><rect x="6" y="4" width="4" height="16" rx="1"/></svg>';
  const ICON_PIP =
    '<svg xmlns="http://www.w3.org/2000/svg" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.85" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M21 9V6a2 2 0 0 0-2-2H4a2 2 0 0 0-2 2v10c0 1.1.9 2 2 2h4"/><rect width="10" height="7" x="12" y="13" rx="2"/></svg>';
  const ICON_JUMP =
    '<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"/><polyline points="15 3 21 3 21 9"/><line x1="10" y1="14" x2="21" y2="3"/></svg>';
  const ICON_MUSIC =
    '<svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.85" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/></svg>';

  function mountIcon(host, name) {
    if (!host || !window.OmniIcons) {
      return;
    }
    OmniIcons.mount(host, name);
  }

  function formatTime(seconds) {
    const s = Math.max(0, Math.floor(Number(seconds) || 0));
    const m = Math.floor(s / 60);
    const rem = s % 60;
    return `${m}:${rem < 10 ? "0" : ""}${rem}`;
  }

  function faviconForMedia(origin, pageUrl) {
    const host = String(origin || "").trim();
    if (host) {
      return `https://www.google.com/s2/favicons?domain=${encodeURIComponent(host)}&sz=32`;
    }
    try {
      const u = new URL(pageUrl);
      return `https://www.google.com/s2/favicons?domain=${encodeURIComponent(u.hostname)}&sz=32`;
    } catch (_) {
      return "";
    }
  }

  function pickPrimary() {
    const list = Array.from(sessions.values()).filter((s) => s && s.active);
    if (!list.length) {
      return null;
    }
    return list.find((s) => s.playing) || list[0];
  }

  function usesOverlay() {
    return (
      window.OmniBridge && typeof OmniBridge.overlayShow === "function"
    );
  }

  function mediaControl(action, tabId, value) {
    if (
      !window.OmniBridge ||
      typeof OmniBridge.browserMediaControl !== "function"
    ) {
      return;
    }
    const targetTab = tabId || primaryTabId;
    const params = { action: String(action || "") };
    if (targetTab) {
      params.tabId = String(targetTab);
    }
    if (value != null && Number.isFinite(Number(value))) {
      params.value = Number(value);
    }
    OmniBridge.browserMediaControl(action, params).catch(() => {});
  }

  function mediaPayload(state) {
    return {
      view: "media",
      tabId: state.tabId,
      playing: Boolean(state.playing),
      paused: Boolean(state.paused),
      title: state.title || "Playing media",
      artist: state.artist || state.origin || "",
      artwork: state.artwork || "",
      currentTime: Number(state.currentTime) || 0,
      duration: Number(state.duration) || 0,
      origin: state.origin || "",
      pageUrl: state.pageUrl || "",
      canPip: Boolean(state.canPip),
    };
  }

  function presentOverlay(state, { keepSize = false } = {}) {
    if (!usesOverlay() || !state || !btn) {
      return;
    }
    const rect = btn.getBoundingClientRect();
    const width = MEDIA_WIDTH;
    let right = Math.round(rect.right);
    const minRight = width + 8;
    right = Math.max(minRight, Math.min(right, window.innerWidth - 8));
    const top = Math.round(rect.bottom + 8);
    lastAnchor = { right, top };
    OmniBridge.overlayShow({
      anchorRight: right,
      anchorTop: top,
      width,
      height: keepSize ? 0 : 148,
      payload: mediaPayload(state),
    }).catch(() => {});
  }

  function markClosed() {
    open = false;
    if (btn) {
      btn.setAttribute("aria-expanded", "false");
    }
    if (window.OmniOverlayManager) {
      OmniOverlayManager.markClosed(OmniOverlayManager.PANEL_MEDIA);
    }
  }

  function hideOverlay() {
    markClosed();
    if (usesOverlay() && typeof OmniBridge.overlayHide === "function") {
      OmniBridge.overlayHide().catch(() => {});
    }
  }

  function setOpen(next) {
    const state = pickPrimary();
    if (next && state) {
      if (window.OmniOverlayManager) {
        OmniOverlayManager.prepareOpen(OmniOverlayManager.PANEL_MEDIA);
      }
      open = true;
      primaryTabId = state.tabId;
      if (btn) {
        btn.setAttribute("aria-expanded", "true");
      }
      presentOverlay(state, { keepSize: false });
      return;
    }
    hideOverlay();
  }

  function syncStartBar() {
    if (!startBar) {
      return;
    }
    const primary = pickPrimary();
    if (!primary || !primary.active) {
      startBar.hidden = true;
      return;
    }

    startBar.hidden = false;

    // Title & Info
    const titleText = primary.title || "Playing media";
    if (startTitle) {
      startTitle.textContent = titleText;
      startTitle.title = titleText;
    }
    if (startArtist) {
      const artistText = primary.artist || primary.origin || "Media";
      startArtist.textContent = artistText;
      startArtist.title = artistText;
    }

    // Artwork
    const artUrl = String(primary.artwork || "").trim();
    const faviconUrl = faviconForMedia(primary.origin, primary.pageUrl);
    const displayImg = artUrl || faviconUrl;
    if (startArt && startFallback) {
      if (displayImg) {
        startArt.src = displayImg;
        startArt.hidden = false;
        startFallback.hidden = true;
        startArt.onerror = () => {
          if (artUrl && faviconUrl && startArt.src !== faviconUrl) {
            startArt.src = faviconUrl;
          } else {
            startArt.hidden = true;
            startFallback.hidden = false;
          }
        };
      } else {
        startArt.removeAttribute("src");
        startArt.hidden = true;
        startFallback.hidden = false;
      }
    }

    // Play/Pause button
    if (startPlayBtn) {
      const isPlaying = Boolean(primary.playing);
      startPlayBtn.innerHTML = isPlaying ? ICON_PAUSE : ICON_PLAY;
      startPlayBtn.setAttribute("aria-label", isPlaying ? "Pause" : "Play");
      startPlayBtn.setAttribute("data-tooltip", isPlaying ? "Pause" : "Play");
    }
  }

  function isStartPageActive() {
    const start = document.getElementById("browser-start");
    const view = document.getElementById("browser-view");
    if (!start) return false;
    if (view && !view.hidden && view.style.display !== "none") {
      return false;
    }
    return !start.hidden && start.style.display !== "none";
  }

  function syncAll() {
    const primary = pickPrimary();
    primaryTabId = primary ? primary.tabId : null;
    const isStart = isStartPageActive();
    // Only show topbar music button if media is active AND NOT on Home/start tab
    const showTopbar = Boolean(primary) && !isStart;

    if (wrap && btn) {
      wrap.hidden = !showTopbar;
      btn.classList.toggle("is-playing", Boolean(primary && primary.playing));
      if (!showTopbar) {
        if (open) {
          hideOverlay();
        }
      } else if (open) {
        presentOverlay(primary, { keepSize: true });
      }
    }

    syncStartBar();
  }

  function upsertMedia(msg) {
    if (!msg || !msg.tabId) {
      return;
    }
    const tabId = String(msg.tabId);
    const active = Boolean(msg.active) || Boolean(msg.playing);
    if (!active) {
      sessions.delete(tabId);
      syncAll();
      return;
    }
    sessions.set(tabId, {
      tabId,
      active: true,
      playing: Boolean(msg.playing),
      paused: Boolean(msg.paused),
      title: String(msg.title || ""),
      artist: String(msg.artist || ""),
      artwork: String(msg.artwork || ""),
      currentTime: Number(msg.currentTime) || 0,
      duration: Number(msg.duration) || 0,
      origin: String(msg.origin || ""),
      pageUrl: String(msg.pageUrl || ""),
      kind: String(msg.kind || ""),
      canPip: Boolean(msg.canPip),
    });
    syncAll();
  }

  function clearTab(tabId) {
    if (!tabId) {
      return;
    }
    sessions.delete(String(tabId));
    syncAll();
  }

  function onOverlayClosed() {
    markClosed();
  }

  function jumpToMediaTab() {
    const primary = pickPrimary();
    if (!primary || !primary.tabId) {
      return;
    }
    if (
      window.OmniBrowser &&
      typeof window.OmniBrowser.activateTab === "function"
    ) {
      window.OmniBrowser.activateTab(primary.tabId);
    }
  }

  function bindStartBarEvents() {
    if (!startBar) {
      return;
    }

    // Set static icons
    if (startPlayBtn) {
      startPlayBtn.innerHTML = ICON_PLAY;
    }
    if (startFallback) {
      startFallback.innerHTML = ICON_MUSIC;
    }

    // Play/Pause button
    startPlayBtn?.addEventListener("click", (event) => {
      event.preventDefault();
      event.stopPropagation();
      const primary = pickPrimary();
      if (primary) {
        const nextPlaying = !primary.playing;
        primary.playing = nextPlaying;
        primary.paused = !nextPlaying;
        startPlayBtn.innerHTML = nextPlaying ? ICON_PAUSE : ICON_PLAY;
        startPlayBtn.setAttribute("aria-label", nextPlaying ? "Pause" : "Play");
        startPlayBtn.setAttribute("data-tooltip", nextPlaying ? "Pause" : "Play");
        mediaControl("toggle", primary.tabId);
      }
    });

    // Jump to media tab when clicking art or info text
    startArtWrap?.addEventListener("click", (event) => {
      event.preventDefault();
      event.stopPropagation();
      jumpToMediaTab();
    });

    startInfo?.addEventListener("click", (event) => {
      event.preventDefault();
      event.stopPropagation();
      jumpToMediaTab();
    });
  }

  function boot() {
    if (window.OmniOverlayManager) {
      OmniOverlayManager.register(OmniOverlayManager.PANEL_MEDIA, {
        close: markClosed,
      });
    }

    if (btn) {
      mountIcon(btn, "music");
      if (wrap) {
        wrap.hidden = true;
      }
      btn.setAttribute("aria-expanded", "false");

      btn.addEventListener("click", (event) => {
        event.preventDefault();
        event.stopPropagation();
        if (wrap && wrap.hidden) {
          return;
        }
        if (open) {
          hideOverlay();
          return;
        }
        if (
          window.OmniOverlayManager &&
          OmniOverlayManager.shouldSuppressOpen(OmniOverlayManager.PANEL_MEDIA)
        ) {
          return;
        }
        setOpen(true);
      });
    }

    bindStartBarEvents();
    syncStartBar();

    document.addEventListener("keydown", (event) => {
      if (event.key === "Escape" && open) {
        hideOverlay();
      }
    });
  }

  window.OmniMedia = {
    boot,
    onMediaEvent: upsertMedia,
    clearTab,
    syncStartBar,
    syncAll,
    onOverlayClosed,
    markClosed,
    isOpen: () => open,
    getPrimarySession: pickPrimary,
  };
})();
