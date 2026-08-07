(() => {
  const MEDIA_WIDTH = 320;
  const sessions = new Map();
  let open = false;
  let primaryTabId = null;
  let lastAnchor = null;

  const wrap = document.getElementById("browser-media-wrap");
  const btn = document.getElementById("browser-media");
  if (!wrap || !btn) {
    return;
  }

  function mountIcon(host, name) {
    if (!host || !window.OmniIcons) {
      return;
    }
    OmniIcons.mount(host, name);
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
    if (!usesOverlay() || !state) {
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

  function hideOverlay() {
    open = false;
    btn.setAttribute("aria-expanded", "false");
    if (usesOverlay() && typeof OmniBridge.overlayHide === "function") {
      OmniBridge.overlayHide().catch(() => {});
    }
  }

  function setOpen(next) {
    const state = pickPrimary();
    if (next && state) {
      open = true;
      primaryTabId = state.tabId;
      btn.setAttribute("aria-expanded", "true");
      presentOverlay(state, { keepSize: false });
      return;
    }
    hideOverlay();
  }

  function syncButton() {
    const primary = pickPrimary();
    primaryTabId = primary ? primary.tabId : null;
    const show = Boolean(primary);
    wrap.hidden = !show;
    btn.classList.toggle("is-playing", Boolean(primary && primary.playing));
    if (!show) {
      if (open) {
        hideOverlay();
      }
      return;
    }
    if (open) {
      presentOverlay(primary, { keepSize: true });
    }
  }

  function upsertMedia(msg) {
    if (!msg || !msg.tabId) {
      return;
    }
    const tabId = String(msg.tabId);
    const active = Boolean(msg.active) || Boolean(msg.playing);
    if (!active) {
      sessions.delete(tabId);
      syncButton();
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
    syncButton();
  }

  function clearTab(tabId) {
    if (!tabId) {
      return;
    }
    sessions.delete(String(tabId));
    syncButton();
  }

  function onOverlayClosed() {
    open = false;
    btn.setAttribute("aria-expanded", "false");
  }

  function boot() {
    mountIcon(btn, "music");
    wrap.hidden = true;
    btn.setAttribute("aria-expanded", "false");

    btn.addEventListener("click", (event) => {
      event.preventDefault();
      event.stopPropagation();
      if (wrap.hidden) {
        return;
      }
      setOpen(!open);
    });

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
    onOverlayClosed,
    isOpen: () => open,
  };
})();
