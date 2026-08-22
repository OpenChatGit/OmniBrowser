(() => {
  // App menu uses native Views menus via CefWindow::ShowMenu — the same
  // path Chrome/Brave use (MenuRunner / MenuHost), not the HTML overlay.
  function bootMenu() {
    const btn = document.getElementById("browser-menu");
    if (!btn) {
      return;
    }

    const hasNative =
      window.OmniBridge &&
      typeof OmniBridge.menuShow === "function" &&
      typeof window.cefQuery === "function";

    let menuOpen = false;

    function openMenu() {
      if (!hasNative) {
        return;
      }
      if (window.OmniOverlayManager) {
        OmniOverlayManager.prepareOpen(OmniOverlayManager.PANEL_MENU);
      }
      const rect = btn.getBoundingClientRect();
      OmniBridge.menuShow({
        // Full button bounds — native mirrors Brave AppMenu TOPRIGHT on
        // GetAnchorBoundsInScreen().
        anchorLeft: Math.round(rect.left),
        anchorTop: Math.round(rect.top),
        anchorRight: Math.round(rect.right),
        anchorBottom: Math.round(rect.bottom),
        payload: {},
      }).catch(() => {});
      menuOpen = true;
      btn.setAttribute("aria-expanded", "true");
    }

    function closeMenu() {
      if (!menuOpen) {
        return;
      }
      menuOpen = false;
      btn.setAttribute("aria-expanded", "false");
      if (window.OmniOverlayManager) {
        OmniOverlayManager.markClosed(OmniOverlayManager.PANEL_MENU);
      }
      if (hasNative && typeof OmniBridge.menuHide === "function") {
        OmniBridge.menuHide().catch(() => {});
      }
    }

    function runCommand(command) {
      const action = command && command.action;
      switch (action) {
        case "navigate":
          if (
            command.url &&
            window.OmniBrowser &&
            typeof OmniBrowser.navigate === "function"
          ) {
            OmniBrowser.navigate(command.url);
          }
          break;
        case "restore-tab":
          if (
            window.OmniBrowser &&
            typeof OmniBrowser.restoreClosedTab === "function"
          ) {
            OmniBrowser.restoreClosedTab(command.closedId);
          }
          break;
        case "open-history":
          if (window.OmniPrivate && OmniPrivate.enabled) {
            break;
          }
          if (
            window.OmniBrowser &&
            typeof OmniBrowser.openTab === "function"
          ) {
            const historyUrl =
              window.OmniBrowserUrl &&
              typeof OmniBrowserUrl.localHistoryUrl === "function"
                ? OmniBrowserUrl.localHistoryUrl()
                : new URL("history.html", window.location.href).href;
            OmniBrowser.openTab(historyUrl);
          }
          break;
        case "open-downloads":
          if (
            window.OmniBrowser &&
            typeof OmniBrowser.openTab === "function"
          ) {
            const downloadsUrl =
              window.OmniBrowserUrl &&
              typeof OmniBrowserUrl.localDownloadsUrl === "function"
                ? OmniBrowserUrl.localDownloadsUrl()
                : new URL("downloads.html", window.location.href).href;
            OmniBrowser.openTab(downloadsUrl);
          }
          break;
        case "open-bookmarks":
          if (
            window.OmniBrowser &&
            typeof OmniBrowser.openTab === "function"
          ) {
            const bookmarksUrl =
              window.OmniBrowserUrl &&
              typeof OmniBrowserUrl.localBookmarksUrl === "function"
                ? OmniBrowserUrl.localBookmarksUrl()
                : new URL("bookmarks.html", window.location.href).href;
            OmniBrowser.openTab(bookmarksUrl);
          }
          break;
        case "open-info":
          if (
            window.OmniBrowser &&
            typeof OmniBrowser.openTab === "function"
          ) {
            const infoUrl =
              window.OmniBrowserUrl &&
              typeof OmniBrowserUrl.localInfoUrl === "function"
                ? OmniBrowserUrl.localInfoUrl()
                : new URL("info.html", window.location.href).href;
            OmniBrowser.openTab(infoUrl);
          }
          break;
        case "new-tab":
          if (window.OmniBrowser && typeof OmniBrowser.openTab === "function") {
            OmniBrowser.openTab();
          }
          break;
        case "new-window":
          if (window.OmniBridge && typeof OmniBridge.windowNew === "function") {
            OmniBridge.windowNew().catch(console.error);
          }
          break;
        case "new-private":
          if (
            window.OmniBridge &&
            typeof OmniBridge.windowNewPrivate === "function"
          ) {
            OmniBridge.windowNewPrivate().catch(console.error);
          }
          break;
        case "clear-data":
          break;
        case "adblock-changed":
          if (
            window.OmniAdblock &&
            typeof OmniAdblock.applyPrefs === "function"
          ) {
            OmniAdblock.applyPrefs({
              enabled: Boolean(payload.enabled),
              aggressive: Boolean(payload.aggressive),
            });
          }
          if (
            window.OmniAdblock &&
            typeof OmniAdblock.refresh === "function"
          ) {
            OmniAdblock.refresh().catch(() => {});
          }
          break;
        case "exit":
          if (
            window.OmniBridge &&
            typeof OmniBridge.windowClose === "function"
          ) {
            OmniBridge.windowClose().catch(console.error);
          }
          break;
        default:
          break;
      }
    }

    btn.addEventListener("click", (event) => {
      event.preventDefault();
      event.stopPropagation();
      if (menuOpen) {
        closeMenu();
        return;
      }
      if (
        window.OmniOverlayManager &&
        OmniOverlayManager.shouldSuppressOpen(OmniOverlayManager.PANEL_MENU)
      ) {
        return;
      }
      openMenu();
    });

    document.addEventListener("keydown", (event) => {
      if (event.key === "Escape" && menuOpen) {
        closeMenu();
        btn.focus();
      }
    });

    window.addEventListener("resize", closeMenu);

    if (hasNative && typeof OmniBridge.browserSubscribe === "function") {
      OmniBridge.browserSubscribe((msg, err) => {
        if (err || !msg) {
          return;
        }
        if (msg.type === "menu" && msg.visible === false) {
          if (menuOpen) {
            menuOpen = false;
            btn.setAttribute("aria-expanded", "false");
            if (window.OmniOverlayManager) {
              OmniOverlayManager.onNativeDismiss(OmniOverlayManager.PANEL_MENU);
            }
          }
        }
        if (msg.type === "menu-command") {
          runCommand(msg.command);
        }
      }).catch(() => {});
    }

    if (window.OmniOverlayManager) {
      OmniOverlayManager.register(OmniOverlayManager.PANEL_MENU, {
        close: closeMenu,
      });
    }
  }

  window.OmniMenu = { bootMenu };
})();
