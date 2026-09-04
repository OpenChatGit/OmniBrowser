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
    let lastDismissTime = 0;

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
      menuOpen = false;
      lastDismissTime = Date.now();
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

    btn.addEventListener("keydown", (event) => {
      if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        if (menuOpen) {
          closeMenu();
        } else {
          openMenu();
        }
      }
    });

    if (window.OmniOverlayManager && typeof OmniOverlayManager.register === "function") {
      OmniOverlayManager.register(OmniOverlayManager.PANEL_MENU, {
        close: closeMenu,
      });
    }

    btn.addEventListener("click", (event) => {
      event.preventDefault();
      event.stopPropagation();
      if (menuOpen) {
        closeMenu();
        return;
      }
      // If a native dismiss happened within the last 150ms (e.g. user clicked this button while menu was open)
      if (Date.now() - lastDismissTime < 150) {
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

    if (hasNative && typeof OmniBridge.browserSubscribe === "function") {
      OmniBridge.browserSubscribe((msg, err) => {
        if (err || !msg) {
          return;
        }
        if (msg.type === "menu" && msg.visible === false) {
          menuOpen = false;
          lastDismissTime = Date.now();
          btn.setAttribute("aria-expanded", "false");
          if (window.OmniOverlayManager) {
            OmniOverlayManager.markClosed(OmniOverlayManager.PANEL_MENU);
          }
        }
        if (msg.type === "menu-command") {
          runCommand(msg.command);
        }
      }).catch(() => {});
    }
  }

  window.OmniMenu = { bootMenu };
})();
