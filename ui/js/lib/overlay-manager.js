(() => {
  const SUPPRESS_MS = 250;
  /** @type {Map<string, { open: boolean, suppressUntil: number, close: (() => void)|null }>} */
  const panels = new Map();

  function panel(id) {
    if (!panels.has(id)) {
      panels.set(id, { open: false, suppressUntil: 0, close: null });
    }
    return panels.get(id);
  }

  window.OmniOverlayManager = {
    PANEL_ADBLOCK: "adblock",
    PANEL_MEDIA: "media",
    PANEL_MENU: "menu",

    register(id, { close } = {}) {
      const entry = panel(id);
      if (typeof close === "function") {
        entry.close = close;
      }
    },

    isOpen(id) {
      return panel(id).open;
    },

    shouldSuppressOpen(id) {
      return Date.now() < panel(id).suppressUntil;
    },

    prepareOpen(id) {
      for (const [otherId, entry] of panels) {
        if (otherId === id || !entry.open) {
          continue;
        }
        entry.open = false;
        if (typeof entry.close === "function") {
          entry.close();
        }
      }
      if (window.OmniTooltip && typeof OmniTooltip.hide === "function") {
        OmniTooltip.hide();
      }
      if (
        window.OmniBrowser &&
        typeof OmniBrowser.releaseTabTip === "function"
      ) {
        OmniBrowser.releaseTabTip();
      }
      panel(id).open = true;
    },

    markClosed(id) {
      panel(id).open = false;
    },

    onNativeDismiss(id) {
      const entry = panel(id);
      if (entry.open) {
        entry.suppressUntil = Date.now() + SUPPRESS_MS;
      }
      entry.open = false;
    },

    onNativeDismissAll() {
      for (const [id, entry] of panels) {
        if (!entry.open) {
          continue;
        }
        entry.suppressUntil = Date.now() + SUPPRESS_MS;
        entry.open = false;
        if (typeof entry.close === "function") {
          entry.close();
        }
      }
    },
  };
})();
