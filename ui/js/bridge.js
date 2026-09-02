(() => {
  function nativeQuery(payload, { persistent = false, onMessage } = {}) {
    return new Promise((resolve, reject) => {
      if (typeof window.cefQuery !== "function") {
        reject(new Error("Native bridge unavailable (cefQuery missing)"));
        return;
      }

      const request = {
        request: JSON.stringify(payload),
        persistent: Boolean(persistent),
        onSuccess(response) {
          try {
            const parsed = response ? JSON.parse(response) : null;
            if (persistent && typeof onMessage === "function") {
              onMessage(parsed);
              return;
            }
            resolve(parsed);
          } catch (err) {
            if (persistent && typeof onMessage === "function") {
              onMessage(null, err);
              return;
            }
            reject(err);
          }
        },
        onFailure(code, message) {
          const err = new Error(message || `Native error ${code}`);
          if (persistent && typeof onMessage === "function") {
            onMessage(null, err);
            return;
          }
          reject(err);
        },
      };

      const queryId = window.cefQuery(request);
      if (persistent) {
        resolve({
          queryId,
          cancel() {
            if (typeof window.cefQueryCancel === "function" && queryId != null) {
              window.cefQueryCancel(queryId);
            }
          },
        });
      }
    });
  }

  window.OmniBridge = {
    /** Generic RPC into the native ApiDispatcher. */
    call(method, params = {}, options = {}) {
      return nativeQuery(
        {
          method: String(method || ""),
          params: params && typeof params === "object" ? params : {},
        },
        options
      );
    },
    windowMinimize() {
      return nativeQuery({ method: "window.minimize", params: {} });
    },
    windowToggleMaximize() {
      return nativeQuery({ method: "window.toggleMaximize", params: {} });
    },
    windowClose() {
      return nativeQuery({ method: "window.close", params: {} });
    },
    windowIsMaximized() {
      return nativeQuery({ method: "window.isMaximized", params: {} });
    },
    windowNew() {
      return nativeQuery({ method: "window.new", params: {} });
    },
    windowNewPrivate() {
      return nativeQuery({ method: "window.newPrivate", params: {} });
    },
    windowCursorPos() {
      return nativeQuery({ method: "window.cursorPos", params: {} });
    },
    windowNewWithTab(tab) {
      return nativeQuery({
        method: "window.newWithTab",
        params: {
          tab: tab && typeof tab === "object" ? tab : {},
        },
      });
    },
    tabConsumePending() {
      return nativeQuery({ method: "tab.consumePending", params: {} });
    },
    browserNavigate(url, tabId) {
      const params = { url };
      if (tabId) {
        params.tabId = String(tabId);
      }
      return nativeQuery({ method: "browser.navigate", params });
    },
    browserEnsureTab(tabId) {
      return nativeQuery({
        method: "browser.ensureTab",
        params: { tabId: String(tabId || "") },
      });
    },
    browserActivateTab(tabId) {
      return nativeQuery({
        method: "browser.activateTab",
        params: { tabId: String(tabId || "") },
      });
    },
    browserCloseTab(tabId) {
      return nativeQuery({
        method: "browser.closeTab",
        params: { tabId: String(tabId || "") },
      });
    },
    browserBack() {
      return nativeQuery({ method: "browser.back", params: {} });
    },
    browserForward() {
      return nativeQuery({ method: "browser.forward", params: {} });
    },
    browserReload(ignoreCache = false) {
      return nativeQuery({
        method: "browser.reload",
        params: { ignoreCache: Boolean(ignoreCache) },
      });
    },
    browserStop() {
      return nativeQuery({ method: "browser.stop", params: {} });
    },
    browserClear() {
      return nativeQuery({ method: "browser.clear", params: {} });
    },
    browserShow(visible = true) {
      return nativeQuery({
        method: "browser.show",
        params: { visible: Boolean(visible) },
      });
    },
    browserSetChromeHeight(height) {
      return nativeQuery({
        method: "browser.setChromeHeight",
        params: { height: Number(height) || 80 },
      });
    },
    browserState() {
      return nativeQuery({ method: "browser.state", params: {} });
    },
    adblockGet(host) {
      const params = {};
      if (host) {
        params.host = String(host);
      }
      return nativeQuery({ method: "browser.adblock.get", params });
    },
    adblockSet(prefs) {
      return nativeQuery({
        method: "browser.adblock.set",
        params: prefs && typeof prefs === "object" ? prefs : {},
      });
    },
    adblockAllowlist(host, allow = true) {
      return nativeQuery({
        method: "browser.adblock.allowlist",
        params: { host: String(host || ""), allow: Boolean(allow) },
      });
    },
    browserSetAudioMuted(muted, tabId) {
      const params = { muted: Boolean(muted) };
      if (tabId) {
        params.tabId = String(tabId);
      }
      return nativeQuery({
        method: "browser.setAudioMuted",
        params,
      });
    },
    browserMediaControl(action, { tabId, value } = {}) {
      const params = { action: String(action || "") };
      if (tabId) {
        params.tabId = String(tabId);
      }
      if (value != null && Number.isFinite(Number(value))) {
        params.value = Number(value);
      }
      return nativeQuery({
        method: "browser.mediaControl",
        params,
      });
    },
    overlayShow(params) {
      return nativeQuery({ method: "overlay.show", params });
    },
    overlayResize(params) {
      return nativeQuery({ method: "overlay.resize", params });
    },
    overlayHide() {
      return nativeQuery({ method: "overlay.hide", params: {} });
    },
    overlayCommand(command) {
      return nativeQuery({ method: "overlay.command", params: { command } });
    },
    menuShow(params) {
      return nativeQuery({ method: "menu.show", params });
    },
    menuHide() {
      return nativeQuery({ method: "menu.hide", params: {} });
    },
    historyList() {
      return nativeQuery({ method: "history.list", params: {} });
    },
    historyRecord(entry) {
      return nativeQuery({
        method: "history.record",
        params: {
          url: entry && entry.url ? String(entry.url) : "",
          title: entry && entry.title ? String(entry.title) : "",
          ts: Number(entry && entry.ts) || Date.now(),
        },
      });
    },
    historyRemove(url) {
      return nativeQuery({
        method: "history.remove",
        params: { url: String(url || "") },
      });
    },
    historyClear() {
      return nativeQuery({ method: "history.clear", params: {} });
    },
    historyImport(entries) {
      return nativeQuery({
        method: "history.import",
        params: { entries: Array.isArray(entries) ? entries : [] },
      });
    },
    bookmarksList() {
      return nativeQuery({ method: "bookmarks.list", params: {} });
    },
    bookmarksRecord(entry) {
      return nativeQuery({
        method: "bookmarks.record",
        params: {
          url: entry && entry.url ? String(entry.url) : "",
          title: entry && entry.title ? String(entry.title) : "",
          ts: Number(entry && entry.ts) || Date.now(),
        },
      });
    },
    bookmarksRemove(url) {
      return nativeQuery({
        method: "bookmarks.remove",
        params: { url: String(url || "") },
      });
    },
    bookmarksClear() {
      return nativeQuery({ method: "bookmarks.clear", params: {} });
    },
    bookmarksImport(entries) {
      return nativeQuery({
        method: "bookmarks.import",
        params: { entries: Array.isArray(entries) ? entries : [] },
      });
    },
    overlaySubscribe(onEvent) {
      return nativeQuery(
        { method: "overlay.subscribe", params: {} },
        {
          persistent: true,
          onMessage(msg, err) {
            if (typeof onEvent === "function") {
              onEvent(msg, err);
            }
          },
        }
      );
    },
    browserSubscribe(onEvent) {
      return nativeQuery(
        { method: "browser.subscribe", params: {} },
        {
          persistent: true,
          onMessage(msg, err) {
            if (typeof onEvent === "function") {
              onEvent(msg, err);
            }
          },
        }
      );
    },
    appInfo() {
      return nativeQuery({ method: "app.info", params: {} });
    },
    apiList() {
      return nativeQuery({ method: "api.list", params: {} });
    },
    settingsGet(key) {
      const params = {};
      if (key != null && String(key).length > 0) {
        params.key = String(key);
      }
      return nativeQuery({ method: "settings.get", params });
    },
    settingsSet(key, value) {
      if (value !== undefined && typeof key === "string") {
        return nativeQuery({ method: "settings.set", params: { key, value } });
      }
      if (key && typeof key === "object") {
        return nativeQuery({ method: "settings.set", params: { settings: key } });
      }
      return nativeQuery({ method: "settings.set", params: key || {} });
    },
    sessionGet() {
      return nativeQuery({ method: "session.get", params: {} });
    },
    sessionSet(session) {
      return nativeQuery({
        method: "session.set",
        params: {
          session: session && typeof session === "object" ? session : {},
        },
      });
    },
    tabsList() {
      return nativeQuery({ method: "tabs.list", params: {} });
    },
    tabsGet(tabId) {
      return nativeQuery({
        method: "tabs.get",
        params: { tabId: String(tabId || "") },
      });
    },
    pluginsList() {
      return nativeQuery({ method: "plugins.list", params: {} });
    },
    pluginsRegister(id, enabled = true) {
      return nativeQuery({
        method: "plugins.register",
        params: { id: String(id || ""), enabled: Boolean(enabled) },
      });
    },
    downloadsList() {
      return nativeQuery({ method: "downloads.list", params: {} });
    },
    downloadsRemove(id) {
      return nativeQuery({
        method: "downloads.remove",
        params: { id: String(id || "") },
      });
    },
    downloadsClear() {
      return nativeQuery({ method: "downloads.clear", params: {} });
    },
    downloadsOpen(path) {
      return nativeQuery({
        method: "downloads.open",
        params: { path: String(path || "") },
      });
    },
    downloadsShowInFolder(path) {
      return nativeQuery({
        method: "downloads.showInFolder",
        params: { path: String(path || "") },
      });
    },
    libraryList() {
      return nativeQuery({ method: "library.list", params: {} });
    },
    libraryAdd(entry) {
      return nativeQuery({
        method: "library.add",
        params: entry && typeof entry === "object" ? entry : {},
      });
    },
    libraryUpdate(entry) {
      return nativeQuery({
        method: "library.update",
        params: entry && typeof entry === "object" ? entry : {},
      });
    },
    libraryRemove(id) {
      return nativeQuery({
        method: "library.remove",
        params: { id: Number(id) || 0 },
      });
    },
    libraryLaunch(id) {
      return nativeQuery({
        method: "library.launch",
        params: { id: Number(id) || 0 },
      });
    },
    libraryPickExe() {
      return nativeQuery({ method: "library.pickExe", params: {} });
    },
    libraryRunning() {
      return nativeQuery({ method: "library.running", params: {} });
    },
    terminalOpen(params = {}) {
      return nativeQuery({ method: "terminal.open", params });
    },
    terminalWrite(id, data, { base64 = false } = {}) {
      return nativeQuery({
        method: "terminal.write",
        params: { id: String(id || ""), data: String(data || ""), base64 },
      });
    },
    terminalResize(id, cols, rows) {
      return nativeQuery({
        method: "terminal.resize",
        params: {
          id: String(id || ""),
          cols: Number(cols) || 80,
          rows: Number(rows) || 24,
        },
      });
    },
    terminalClose(id) {
      return nativeQuery({
        method: "terminal.close",
        params: { id: String(id || "") },
      });
    },
    terminalSubscribe(id, onEvent) {
      return nativeQuery(
        { method: "terminal.subscribe", params: { id: String(id || "") } },
        {
          persistent: true,
          onMessage(msg, err) {
            if (typeof onEvent === "function") {
              onEvent(msg, err);
            }
          },
        }
      );
    },
    gitStatus(cwd) {
      return nativeQuery({
        method: "git.status",
        params: { cwd: String(cwd || "") },
      });
    },
    adblockCosmetics(url) {
      return nativeQuery({
        method: "browser.adblock.cosmetics",
        params: { url: String(url || "") },
      });
    },

    agentPause() {
      return nativeQuery({ method: "agent.pause", params: {} });
    },
  };
})();
