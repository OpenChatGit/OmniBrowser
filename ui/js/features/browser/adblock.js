(() => {
  const PANEL_WIDTH = 320;

  function hostFromUrl(url) {
    if (!url || typeof url !== "string") {
      return "";
    }
    try {
      const u = new URL(url);
      if (u.protocol !== "http:" && u.protocol !== "https:") {
        return "";
      }
      return (u.hostname || "").replace(/^www\./i, "");
    } catch {
      return "";
    }
  }

  function formatCount(n) {
    return (Number(n) || 0).toLocaleString();
  }

  function currentPageUrl() {
    if (
      window.OmniBrowser &&
      typeof OmniBrowser.getCurrentUrl === "function"
    ) {
      return OmniBrowser.getCurrentUrl() || "";
    }
    const input = document.getElementById("browser-search-input");
    return input && input.value ? String(input.value).trim() : "";
  }

  function boot() {
    const btn = document.getElementById("browser-adblock");
    const statsToggle = document.getElementById("browser-start-stats-toggle");
    const statsCard = document.getElementById("browser-start-stats-card");
    const statsValue = document.getElementById("browser-start-stats-value");
    if (!btn || !window.OmniBridge) {
      return;
    }

    let prefs = {
      enabled: true,
      aggressive: false,
      allowlist: [],
      blockedTotal: 0,
      blockedForHost: 0,
      host: "",
      siteShieldsUp: true,
    };
    let open = false;
    let pollTimer = null;
    let fallbackPanel = null;

    OmniIcons.mount(btn, "shield");

    function usesOverlay() {
      return typeof OmniBridge.overlayShow === "function";
    }

    function siteShieldsUp() {
      if (!prefs.enabled) {
        return false;
      }
      const host = hostFromUrl(currentPageUrl());
      if (!host) {
        return Boolean(prefs.enabled);
      }
      const list = Array.isArray(prefs.allowlist) ? prefs.allowlist : [];
      return !list.some((entry) => {
        const e = String(entry || "").replace(/^www\./i, "");
        return e === host || host.endsWith(`.${e}`);
      });
    }

    function syncButton() {
      const on = siteShieldsUp();
      btn.classList.toggle("is-on", on);
      btn.classList.toggle("is-off", !on);
      btn.classList.toggle("is-open", open);
      btn.setAttribute("aria-expanded", open ? "true" : "false");
      btn.setAttribute(
        "aria-label",
        on ? "Ad blocking on" : "Ad blocking off"
      );
      const host = hostFromUrl(currentPageUrl());
      btn.setAttribute(
        "data-tooltip",
        on
          ? host
            ? `Shields up for ${host}`
            : "Ad blocking on"
          : host
            ? `Shields down for ${host}`
            : "Ad blocking off"
      );
    }

    function shieldsPayload() {
      const pageUrl = currentPageUrl();
      const host = hostFromUrl(pageUrl) || prefs.host || "";
      const up = siteShieldsUp();
      return {
        view: "shields",
        host,
        pageUrl,
        enabled: Boolean(prefs.enabled),
        aggressive: Boolean(prefs.aggressive),
        siteShieldsUp: up,
        blockedForHost: Number(prefs.blockedForHost) || 0,
        blockedTotal: Number(prefs.blockedTotal) || 0,
        advancedOpen: false,
      };
    }

    function presentOverlay({ keepSize = false } = {}) {
      if (!usesOverlay()) {
        return;
      }
      const rect = btn.getBoundingClientRect();
      let right = Math.round(rect.right);
      right = Math.max(
        PANEL_WIDTH + 8,
        Math.min(right, window.innerWidth - 8)
      );
      const top = Math.round(rect.bottom + 8);
      OmniBridge.overlayShow({
        anchorRight: right,
        anchorTop: top,
        width: PANEL_WIDTH,
        height: keepSize ? 0 : 220,
        payload: shieldsPayload(),
      }).catch(() => {});
    }

    function markClosed() {
      open = false;
      if (pollTimer) {
        clearInterval(pollTimer);
        pollTimer = null;
      }
      syncButton();
      if (window.OmniOverlayManager) {
        OmniOverlayManager.markClosed(OmniOverlayManager.PANEL_ADBLOCK);
      }
    }

    function hideOverlay() {
      markClosed();
      if (fallbackPanel) {
        fallbackPanel.hidden = true;
      }
      if (usesOverlay() && typeof OmniBridge.overlayHide === "function") {
        OmniBridge.overlayHide().catch(() => {});
      }
    }

    function ensureFallbackPanel() {
      if (fallbackPanel) {
        return fallbackPanel;
      }
      fallbackPanel = document.createElement("div");
      fallbackPanel.className = "omni-shield-panel";
      fallbackPanel.hidden = true;
      fallbackPanel.innerHTML =
        '<div class="omni-shield-fallback">Ad blocking panel (native overlay unavailable)</div>';
      document.body.appendChild(fallbackPanel);
      return fallbackPanel;
    }

    function setOpen(next) {
      if (next) {
        if (window.OmniOverlayManager) {
          OmniOverlayManager.prepareOpen(OmniOverlayManager.PANEL_ADBLOCK);
        }
        open = true;
        syncButton();
        if (usesOverlay()) {
          presentOverlay({ keepSize: false });
        } else {
          const node = ensureFallbackPanel();
          node.hidden = false;
          const rect = btn.getBoundingClientRect();
          node.style.left = `${Math.max(8, rect.right - PANEL_WIDTH)}px`;
          node.style.top = `${rect.bottom + 8}px`;
        }
        if (pollTimer) {
          clearInterval(pollTimer);
        }
        pollTimer = setInterval(() => {
          refresh({ keepOpen: true }).catch(() => {});
        }, 1500);
        return;
      }
      hideOverlay();
    }

    async function refresh({ keepOpen = false } = {}) {
      const host = hostFromUrl(currentPageUrl());
      try {
        prefs = await OmniBridge.adblockGet(host);
      } catch {
        /* preview without native */
      }
      syncButton();
      if (statsValue) {
        statsValue.textContent = formatCount(prefs.blockedTotal);
      }
      if (keepOpen && open) {
        if (usesOverlay()) {
          presentOverlay({ keepSize: true });
        }
      }
      return prefs;
    }

    btn.addEventListener("keydown", (event) => {
      if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        if (open) {
          hideOverlay();
        } else {
          setOpen(true);
        }
      }
    });

    if (window.OmniOverlayManager && typeof OmniOverlayManager.bindToggle === "function") {
      OmniOverlayManager.register(OmniOverlayManager.PANEL_ADBLOCK, {
        close: hideOverlay,
      });
      OmniOverlayManager.bindToggle(OmniOverlayManager.PANEL_ADBLOCK, btn, {
        isOpen: () => open,
        open: () => setOpen(true),
        close: hideOverlay,
      });
    } else {
      btn.addEventListener("click", (event) => {
        event.preventDefault();
        event.stopPropagation();
        if (open) {
          hideOverlay();
          return;
        }
        setOpen(true);
      });
    }

    document.addEventListener("keydown", (event) => {
      if (event.key === "Escape" && open) {
        hideOverlay();
      }
    });

    let statsTimer = null;
    if (statsToggle && statsCard) {
      statsToggle.addEventListener("click", async () => {
        const show = statsCard.hidden;
        statsCard.hidden = !show;
        statsToggle.setAttribute("aria-expanded", show ? "true" : "false");
        statsToggle.textContent = show ? "Hide Statistics" : "Show Statistics";
        if (statsTimer) {
          clearInterval(statsTimer);
          statsTimer = null;
        }
        if (show) {
          await refresh();
          statsTimer = setInterval(() => {
            refresh().catch(() => {});
          }, 2000);
        }
      });
    }

    window.OmniAdblock = {
      refresh,
      closePanel: hideOverlay,
      markClosed,
      onOverlayClosed() {
        markClosed();
      },
      applyPrefs(next) {
        if (next && typeof next === "object") {
          prefs = { ...prefs, ...next };
          syncButton();
          if (statsValue) {
            statsValue.textContent = formatCount(prefs.blockedTotal);
          }
          if (open && usesOverlay()) {
            presentOverlay({ keepSize: true });
          }
        }
      },
      isOpen: () => open,
    };

    refresh().catch(() => {
      syncButton();
    });
  }

  window.OmniAdblock = { boot };
})();
