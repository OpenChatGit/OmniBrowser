(() => {
  const SUPPRESS_MS = 160;
  /** @type {Map<string, { open: boolean, suppressUntil: number, close: (() => void)|null }>} */
  const panels = new Map();
  let dismissing = false;

  function panel(id) {
    if (!panels.has(id)) {
      panels.set(id, { open: false, suppressUntil: 0, close: null });
    }
    return panels.get(id);
  }

  function suppress(entry) {
    entry.suppressUntil = Date.now() + SUPPRESS_MS;
  }

  window.OmniOverlayManager = {
    PANEL_ADBLOCK: "adblock",
    PANEL_MEDIA: "media",
    PANEL_MENU: "menu",
    PANEL_ENGINE: "engine",

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
      panel(id).suppressUntil = 0;
    },

    markClosed(id) {
      panel(id).open = false;
    },

    onNativeDismiss(id) {
      const entry = panel(id);
      suppress(entry);
      entry.open = false;
    },

    onNativeDismissAll() {
      if (dismissing) {
        return;
      }
      dismissing = true;
      try {
        for (const [id, entry] of panels) {
          if (id === window.OmniOverlayManager.PANEL_MENU) {
            continue;
          }
          suppress(entry);
          const wasOpen = entry.open;
          entry.open = false;
          if (wasOpen && typeof entry.close === "function") {
            entry.close();
          }
        }
      } finally {
        dismissing = false;
      }
    },

    bindToggle(id, button, { isOpen, open, close } = {}) {
      if (!button || typeof isOpen !== "function") {
        return;
      }
      const entry = panel(id);
      if (typeof close === "function") {
        entry.close = close;
      }

      button.addEventListener("click", (event) => {
        event.preventDefault();
        event.stopPropagation();
        if (isOpen()) {
          if (typeof close === "function") {
            close();
          }
          suppress(entry);
          return;
        }
        if (Date.now() < entry.suppressUntil) {
          return;
        }
        if (typeof open === "function") {
          open();
        }
      });
    },
  };
})();
