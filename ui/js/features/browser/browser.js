(() => {
  const {
    toNavigationUrl,
    addressBarLabel,
    titleFromUrl,
    domainFromUrl,
    normalizeTabTitle,
    isBlankUrl,
    isLocalSearchUrl,
    isLocalHistoryUrl,
    isLocalDownloadsUrl,
    isLocalBookmarksUrl,
    isLocalInfoUrl,
    localDownloadsUrl,
    formatTabTitle,
    urlsMatch,
    measureChromeHeight,
    listSearchEngines,
    getSearchEngine,
    setSearchEngine,
  } = window.OmniBrowserUrl;

  const NEW_TAB_FAVICON = "assets/QuBrain-new/q-white.svg";
  const SESSION_KEY = "omni.browser.session";
  const SESSION_VERSION = 1;
  const HISTORY_KEY = "omni.browser.history";
  const BOOKMARKS_KEY = "omni.browser.bookmarks";
  const CLOSED_KEY = "omni.browser.closed";
  const MAX_TAB_HISTORY = 40;
  const MAX_VISIT_HISTORY = 500;
  const MAX_BOOKMARKS = 500;
  const HISTORY_MIGRATED_KEY = "omni.browser.historyMigrated";
  const BOOKMARKS_MIGRATED_KEY = "omni.browser.bookmarksMigrated";
  const MAX_CLOSED_TABS = 20;
  const isPrivate = Boolean(window.OmniPrivate && OmniPrivate.enabled);

  function bootBrowser() {
    const root = document.getElementById("browser");
    const start = document.getElementById("browser-start");
    const view = document.getElementById("browser-view");
    const tabsEl = document.getElementById("browser-tabs");
    const tabNew = document.getElementById("browser-tab-new");
    const searchForm = document.getElementById("browser-search-form");

    const NOTICE_KEY = "omni.ui.previewNoticeDismissed";
    const notice = document.getElementById("browser-start-notice");
    const noticeClose = document.getElementById("browser-start-notice-close");
    try {
      if (notice && localStorage.getItem(NOTICE_KEY) === "1") {
        notice.hidden = true;
      }
    } catch (_) {
      // ignore
    }
    noticeClose?.addEventListener("click", () => {
      if (notice) {
        notice.hidden = true;
      }
      try {
        localStorage.setItem(NOTICE_KEY, "1");
      } catch (_) {
        // ignore
      }
    });
    const searchInput = document.getElementById("browser-search-input");
    const searchMark = document.getElementById("browser-search-mark");
    const startForm = document.getElementById("browser-start-search-form");
    const startInput = document.getElementById("browser-start-search-input");
    const startGo = startForm
      ? startForm.querySelector(".browser-search-go")
      : null;
    const btnBack = document.getElementById("browser-back");
    const btnForward = document.getElementById("browser-forward");
    const btnReload = document.getElementById("browser-reload");
    const btnBookmark = document.getElementById("browser-bookmark");
    const btnMenu = document.getElementById("browser-menu");
    const btnDownload = document.getElementById("browser-download");
    const btnDownloadIcon = document.getElementById("browser-download-icon");
    const btnDownloadProgress = btnDownload
      ? btnDownload.querySelector(".topbar-download-progress")
      : null;
    const engineBtn = document.getElementById("browser-engine");
    const engineIcon = document.getElementById("browser-engine-icon");
    const engineMenu = document.getElementById("browser-engine-menu");
    const clockRoot = document.getElementById("browser-start-clock");
    const clockTime = document.getElementById("browser-start-clock-time");
    const clockPeriod = document.getElementById("browser-start-clock-period");

    if (!root || !start || !view || !searchForm || !searchInput || !tabsEl) {
      return;
    }

    const hasNative =
      window.OmniBridge &&
      typeof window.OmniBridge.browserNavigate === "function";

    function usesNativeStore() {
      return hasNative && !isPrivate;
    }

    let seq = 0;
    /** @type {{ id: string, title: string, history: string[], index: number }[]} */
    let tabs = [];
    let activeId = null;
    let addressEditing = false;
    let nativeCanBack = false;
    let nativeCanForward = false;
    let nativeLoading = false;
    let pendingTraverse = 0;
    let lastPushedUrl = "";
    let navSerial = 0;
    let lastChromeHeight = -1;
    /** Per-tab audible state (background tabs can keep playing). */
    let contentAudioPlaying = false;
    let contentAudioMuted = false;
    /** @type {Map<string, boolean>} */
    const tabAudioPlaying = new Map();
    /** @type {Map<string, boolean>} */
    const tabAudioMuted = new Map();
    let tabTipToken = 0;
    /** @type {string|null} */
    let tabTipId = null;
    let tabTipTimer = 0;
    let tabTipOverlayOpen = false;
    let tabTipMemoryPoll = 0;
    let tabTipDataKey = "";
    /** @type {Map<string, number>} */
    const tabMemoryMb = new Map();
    /** @type {number|null} */
    const aiPill = document.getElementById("ai-control-pill");
    const aiPauseBtn = document.getElementById("ai-control-pause-btn");
    let aiPaused = false;

    const aiBtnIcon = document.getElementById("ai-control-btn-icon");
    if (aiBtnIcon && window.OmniIcons) {
      OmniIcons.mount(aiBtnIcon, "hand");
    }

    if (aiPauseBtn) {
      aiPauseBtn.addEventListener("click", (e) => {
        e.preventDefault();
        e.stopPropagation();
        aiPaused = true;
        if (aiActiveTimer) {
          window.clearTimeout(aiActiveTimer);
          aiActiveTimer = 0;
        }
        document.body.classList.remove("ai-active");
        if (root) root.classList.remove("ai-active");
        if (aiPill) aiPill.hidden = true;
        if (window.OmniBridge && typeof OmniBridge.agentPause === "function") {
          OmniBridge.agentPause().catch(() => {});
        }
      });
    }

    let lastContentMemoryMb = null;
    let downloadHideTimer = 0;
    let aiActiveTimer = 0;
    const DOWNLOAD_RING = 2 * Math.PI * 14.5;
    const TAB_TIP_WIDTH = 280;
    const TAB_TIP_DELAY_MS = 420;
    // Memory is cosmetic; avoid hammering browser.state while the tip is open.
    const TAB_TIP_MEMORY_POLL_MS = 4000;

    /** @type {{ url: string, title: string, ts: number }[]} */
    let bookmarkEntries = loadBookmarks();

    /** @type {{ url: string, title: string, ts: number }[]} */
    let visitHistory = loadVisitHistory();
    /** @type {{ id: string, title: string, history: string[], index: number, ts: number }[]} */
    let closedTabs = loadClosedTabs();

    function loadBookmarks() {
      try {
        const raw = localStorage.getItem(BOOKMARKS_KEY);
        const data = raw ? JSON.parse(raw) : null;
        if (!Array.isArray(data)) {
          return [];
        }
        return data
          .map((entry) => {
            if (typeof entry === "string") {
              return entry
                ? {
                    url: entry,
                    title: titleFromUrl(entry),
                    ts: 0,
                  }
                : null;
            }
            if (
              !entry ||
              typeof entry.url !== "string" ||
              !entry.url ||
              isBlankUrl(entry.url)
            ) {
              return null;
            }
            return {
              url: entry.url,
              title: String(entry.title || titleFromUrl(entry.url)),
              ts: Number(entry.ts) || 0,
            };
          })
          .filter(Boolean)
          .slice(0, MAX_BOOKMARKS);
      } catch (_) {
        return [];
      }
    }

    function saveBookmarks() {
      if (usesNativeStore()) {
        return;
      }
      try {
        localStorage.setItem(BOOKMARKS_KEY, JSON.stringify(bookmarkEntries));
      } catch (_) {
        /* ignore */
      }
    }

    function isBookmarked(url) {
      if (!url) {
        return false;
      }
      return bookmarkEntries.some((entry) => urlsMatch(entry.url, url));
    }

    function addBookmark(url, title) {
      if (!url || isBlankUrl(url) || isLocalBookmarksUrl(url)) {
        return;
      }
      const cleanTitle = normalizeTabTitle(title || titleFromUrl(url), url);
      const ts = Date.now();
      bookmarkEntries = bookmarkEntries.filter((entry) => !urlsMatch(entry.url, url));
      bookmarkEntries.unshift({ url, title: cleanTitle, ts });
      if (bookmarkEntries.length > MAX_BOOKMARKS) {
        bookmarkEntries.length = MAX_BOOKMARKS;
      }
      saveBookmarks();
      syncBookmarkButton();
      if (
        hasNative &&
        window.OmniBridge &&
        typeof OmniBridge.bookmarksRecord === "function"
      ) {
        OmniBridge.bookmarksRecord({ url, title: cleanTitle, ts }).catch(
          () => {}
        );
      }
    }

    function removeBookmark(url) {
      if (!url) {
        return;
      }
      const before = bookmarkEntries.length;
      bookmarkEntries = bookmarkEntries.filter((entry) => !urlsMatch(entry.url, url));
      if (bookmarkEntries.length === before) {
        return;
      }
      saveBookmarks();
      syncBookmarkButton();
      if (
        hasNative &&
        window.OmniBridge &&
        typeof OmniBridge.bookmarksRemove === "function"
      ) {
        OmniBridge.bookmarksRemove(url).catch(() => {});
      }
    }

    function migrateBookmarksToNative() {
      if (
        !hasNative ||
        !window.OmniBridge ||
        typeof OmniBridge.bookmarksImport !== "function"
      ) {
        return;
      }
      try {
        if (localStorage.getItem(BOOKMARKS_MIGRATED_KEY) === "1") {
          return;
        }
      } catch (_) {
        /* continue */
      }
      if (!bookmarkEntries.length) {
        try {
          localStorage.setItem(BOOKMARKS_MIGRATED_KEY, "1");
        } catch (_) {
          /* ignore */
        }
        return;
      }
      OmniBridge.bookmarksImport(bookmarkEntries)
        .then(() => {
          try {
            localStorage.setItem(BOOKMARKS_MIGRATED_KEY, "1");
          } catch (_) {
            /* ignore */
          }
          pullBookmarksFromNative();
        })
        .catch(() => {});
    }

    function pullBookmarksFromNative() {
      if (
        !hasNative ||
        !window.OmniBridge ||
        typeof OmniBridge.bookmarksList !== "function"
      ) {
        return;
      }
      OmniBridge.bookmarksList()
        .then((result) => {
          if (!result || !Array.isArray(result.entries)) {
            return;
          }
          bookmarkEntries = result.entries
            .filter(
              (entry) =>
                entry &&
                typeof entry.url === "string" &&
                entry.url &&
                !isBlankUrl(entry.url)
            )
            .map((entry) => ({
              url: entry.url,
              title: String(entry.title || titleFromUrl(entry.url)),
              ts: Number(entry.ts) || 0,
            }))
            .slice(0, MAX_BOOKMARKS);
          if (!usesNativeStore()) {
            saveBookmarks();
          }
          syncBookmarkButton();
        })
        .catch(() => {});
    }

    function loadVisitHistory() {
      if (isPrivate) {
        return [];
      }
      if (usesNativeStore()) {
        return [];
      }
      try {
        const raw = localStorage.getItem(HISTORY_KEY);
        const data = raw ? JSON.parse(raw) : null;
        if (!Array.isArray(data)) {
          return [];
        }
        return data
          .filter(
            (entry) =>
              entry &&
              typeof entry.url === "string" &&
              entry.url &&
              !isBlankUrl(entry.url)
          )
          .map((entry) => ({
            url: entry.url,
            title: String(entry.title || titleFromUrl(entry.url)),
            ts: Number(entry.ts) || 0,
          }))
          .slice(0, MAX_VISIT_HISTORY);
      } catch (_) {
        return [];
      }
    }

    function saveVisitHistory() {
      if (isPrivate || usesNativeStore()) {
        return;
      }
      try {
        localStorage.setItem(HISTORY_KEY, JSON.stringify(visitHistory));
      } catch (_) {
        /* ignore */
      }
    }

    function loadClosedTabs() {
      if (isPrivate) {
        return [];
      }
      try {
        const raw = localStorage.getItem(CLOSED_KEY);
        const data = raw ? JSON.parse(raw) : null;
        if (!Array.isArray(data)) {
          return [];
        }
        return data
          .filter((entry) => entry && Array.isArray(entry.history))
          .slice(0, MAX_CLOSED_TABS);
      } catch (_) {
        return [];
      }
    }

    function saveClosedTabs() {
      if (isPrivate) {
        return;
      }
      try {
        localStorage.setItem(CLOSED_KEY, JSON.stringify(closedTabs));
      } catch (_) {
        /* ignore */
      }
    }

    function rememberVisit(url, title) {
      if (isPrivate) {
        return;
      }
      if (
        !url ||
        isBlankUrl(url) ||
        isLocalHistoryUrl(url) ||
        isLocalDownloadsUrl(url) ||
        isLocalBookmarksUrl(url) ||
        isLocalInfoUrl(url)
      ) {
        return;
      }
      const cleanTitle = normalizeTabTitle(title || titleFromUrl(url), url);
      const ts = Date.now();
      visitHistory = visitHistory.filter((entry) => entry.url !== url);
      visitHistory.unshift({
        url,
        title: cleanTitle,
        ts,
      });
      if (visitHistory.length > MAX_VISIT_HISTORY) {
        visitHistory.length = MAX_VISIT_HISTORY;
      }
      saveVisitHistory();
      if (
        hasNative &&
        window.OmniBridge &&
        typeof OmniBridge.historyRecord === "function"
      ) {
        OmniBridge.historyRecord({ url, title: cleanTitle, ts }).catch(() => {});
      }
    }

    function migrateVisitHistoryToNative() {
      if (isPrivate) {
        return;
      }
      if (
        !hasNative ||
        !window.OmniBridge ||
        typeof OmniBridge.historyImport !== "function"
      ) {
        return;
      }
      try {
        if (localStorage.getItem(HISTORY_MIGRATED_KEY) === "1") {
          return;
        }
      } catch (_) {
        /* continue */
      }
      if (!visitHistory.length) {
        try {
          localStorage.setItem(HISTORY_MIGRATED_KEY, "1");
        } catch (_) {
          /* ignore */
        }
        return;
      }
      OmniBridge.historyImport(visitHistory)
        .then(() => {
          try {
            localStorage.setItem(HISTORY_MIGRATED_KEY, "1");
          } catch (_) {
            /* ignore */
          }
          pullVisitHistoryFromNative();
        })
        .catch(() => {});
    }

    function pullVisitHistoryFromNative() {
      if (
        !hasNative ||
        !window.OmniBridge ||
        typeof OmniBridge.historyList !== "function"
      ) {
        return;
      }
      OmniBridge.historyList()
        .then((result) => {
          if (!result || !Array.isArray(result.entries)) {
            return;
          }
          visitHistory = result.entries
            .filter(
              (entry) =>
                entry &&
                typeof entry.url === "string" &&
                entry.url &&
                !isBlankUrl(entry.url)
            )
            .map((entry) => ({
              url: entry.url,
              title: String(entry.title || titleFromUrl(entry.url)),
              ts: Number(entry.ts) || 0,
            }))
            .slice(0, MAX_VISIT_HISTORY);
        })
        .catch(() => {});
    }

    function listHistory(limit = 40) {
      return visitHistory.slice(0, limit).map((entry) => ({
        url: entry.url,
        title: entry.title,
        ts: entry.ts,
      }));
    }

    function listRecentTabs(limit = 10) {
      /** @type {{ type: string, title: string, url: string, closedId?: string, shortcut?: string, favicons?: string[] }[]} */
      const items = [];
      closedTabs.slice(0, 5).forEach((entry, index) => {
        const url =
          entry.index >= 0 && entry.history[entry.index]
            ? entry.history[entry.index]
            : entry.history[entry.history.length - 1] || "";
        if (!url || isBlankUrl(url)) {
          return;
        }
        items.push({
          type: "closed",
          title: entry.title || titleFromUrl(url),
          url,
          closedId: entry.id,
          shortcut: index === 0 ? "Ctrl+Shift+T" : "",
          favicons: faviconCandidatesForUrl(url),
        });
      });
      for (const visit of visitHistory) {
        if (items.length >= limit) {
          break;
        }
        if (items.some((item) => item.url === visit.url)) {
          continue;
        }
        items.push({
          type: "visit",
          title: visit.title,
          url: visit.url,
          favicons: faviconCandidatesForUrl(visit.url),
        });
      }
      return items;
    }

    function restoreClosedTab(closedId) {
      if (!closedTabs.length) {
        return false;
      }
      let target = null;
      if (closedId) {
        const idx = closedTabs.findIndex((t) => t.id === closedId);
        if (idx >= 0) {
          target = closedTabs.splice(idx, 1)[0];
        }
      }
      if (!target) {
        target = closedTabs.shift();
      }
      if (!target) {
        return false;
      }
      saveClosedTabs();
      seq += 1;
      const history = Array.isArray(target.history) ? [...target.history] : [];
      const tab = {
        id: `tab-${seq}`,
        title: target.title || "New Tab",
        history,
        index:
          history.length === 0
            ? -1
            : Math.min(
                history.length - 1,
                Math.max(0, Number.isInteger(target.index) ? target.index : 0)
              ),
        contentLive: false,
      };
      tabs.push(tab);
      activeId = tab.id;
      applyActiveTab();
      renderTabs();
      scheduleSaveSession();
      return true;
    }

    let saveTimer = 0;

    function scheduleSaveSession() {
      if (saveTimer) {
        window.clearTimeout(saveTimer);
      }
      saveTimer = window.setTimeout(() => {
        saveTimer = 0;
        saveSession();
      }, 120);
    }

    function buildSessionPayload() {
      return {
        v: SESSION_VERSION,
        seq,
        activeId,
        tabs: tabs.map((tab) => {
          const history = Array.isArray(tab.history)
            ? tab.history.filter((entry) => typeof entry === "string")
            : [];
          const currentUrl =
            Number.isInteger(tab.index) && tab.index >= 0
              ? history[tab.index] || ""
              : "";
          let trimmed =
            history.length > MAX_TAB_HISTORY
              ? history.slice(history.length - MAX_TAB_HISTORY)
              : history.slice();
          let index = Number.isInteger(tab.index) ? tab.index : -1;
          if (trimmed.length === 0) {
            index = -1;
          } else if (
            currentUrl &&
            history.length > trimmed.length &&
            !trimmed.includes(currentUrl)
          ) {
            trimmed.push(currentUrl);
            if (trimmed.length > MAX_TAB_HISTORY) {
              trimmed = trimmed.slice(trimmed.length - MAX_TAB_HISTORY);
            }
            index = trimmed.length - 1;
          } else if (history.length > trimmed.length) {
            index = Math.min(
              trimmed.length - 1,
              Math.max(0, index - (history.length - trimmed.length))
            );
          } else {
            index = Math.min(trimmed.length - 1, Math.max(-1, index));
          }
          if (currentUrl) {
            const at = trimmed.indexOf(currentUrl);
            if (at >= 0) {
              index = at;
            }
          }
          return {
            id: String(tab.id),
            title: String(tab.title || "New Tab"),
            history: trimmed,
            index,
          };
        }),
        closedTabs: closedTabs.slice(0, MAX_CLOSED_TABS),
      };
    }

    function applySessionData(data) {
      if (
        !data ||
        data.v !== SESSION_VERSION ||
        !Array.isArray(data.tabs) ||
        data.tabs.length === 0
      ) {
        return false;
      }
      const restored = data.tabs
        .map((tab) => {
          if (!tab || typeof tab.id !== "string") {
            return null;
          }
          const history = Array.isArray(tab.history)
            ? tab.history.filter(
                (entry) => typeof entry === "string" && entry.length > 0
              )
            : [];
          let index = Number.isInteger(tab.index) ? tab.index : -1;
          if (history.length === 0) {
            index = -1;
          } else {
            index = Math.min(history.length - 1, Math.max(0, index));
          }
          return {
            id: tab.id,
            title: normalizeTabTitle(
              tab.title || "New Tab",
              index >= 0 ? history[index] : ""
            ),
            history,
            index,
            contentLive: false,
          };
        })
        .filter(Boolean);
      if (!restored.length) {
        return false;
      }
      tabs = restored;
      if (Array.isArray(data.closedTabs)) {
        closedTabs = data.closedTabs
          .filter((entry) => entry && Array.isArray(entry.history))
          .slice(0, MAX_CLOSED_TABS);
        saveClosedTabs();
      }
      seq = Math.max(
        Number(data.seq) || 0,
        ...restored.map((tab) => {
          const match = /^tab-(\d+)$/.exec(tab.id);
          return match ? Number(match[1]) : 0;
        })
      );
      activeId =
        typeof data.activeId === "string" &&
        restored.some((tab) => tab.id === data.activeId)
          ? data.activeId
          : restored[0].id;
      return true;
    }

    function syncSessionToNative(payload) {
      if (
        isPrivate ||
        !hasNative ||
        !window.OmniBridge ||
        typeof OmniBridge.sessionSet !== "function"
      ) {
        return;
      }
      OmniBridge.sessionSet({ session: payload }).catch(() => {});
    }

    function saveSession() {
      if (isPrivate) {
        return;
      }
      try {
        const payload = buildSessionPayload();
        localStorage.setItem(SESSION_KEY, JSON.stringify(payload));
        syncSessionToNative(payload);
      } catch (_) {
        /* quota / private mode */
      }
    }

    function restoreSession() {
      if (isPrivate) {
        return false;
      }
      try {
        const raw = localStorage.getItem(SESSION_KEY);
        if (!raw) {
          return false;
        }
        const data = JSON.parse(raw);
        return applySessionData(data);
      } catch (_) {
        return false;
      }
    }

    function restoreSessionFromNative() {
      if (
        isPrivate ||
        !hasNative ||
        !window.OmniBridge ||
        typeof OmniBridge.sessionGet !== "function"
      ) {
        return Promise.resolve(false);
      }
      return OmniBridge.sessionGet()
        .then((result) => {
          if (!result || !result.session) {
            return false;
          }
          return applySessionData(result.session);
        })
        .catch(() => false);
    }

    function createTab() {
      seq += 1;
      return {
        id: `tab-${seq}`,
        title: "New Tab",
        history: [],
        index: -1,
        contentLive: false,
      };
    }

    function activeTab() {
      return tabs.find((tab) => tab.id === activeId) || null;
    }

    /** Only replace titles that are still hostname placeholders. */
    function setProvisionalTitle(tab, url, previousUrl) {
      if (!tab || !url || url === "about:blank") {
        return;
      }
      const prev = previousUrl || "";
      if (
        !tab.title ||
        tab.title === "New Tab" ||
        (prev && tab.title === titleFromUrl(prev))
      ) {
        tab.title = titleFromUrl(url);
      }
    }

    function currentUrl() {
      const tab = activeTab();
      if (!tab || tab.index < 0) {
        return "";
      }
      return tab.history[tab.index] || "";
    }

    function syncBookmarkButton() {
      if (!btnBookmark) {
        return;
      }
      const url = currentUrl();
      const active = Boolean(url && isBookmarked(url));
      btnBookmark.classList.toggle("is-active", active);
      btnBookmark.setAttribute("aria-pressed", active ? "true" : "false");
      btnBookmark.disabled = !url;
    }

    function syncEngineButton() {
      if (!engineBtn || !engineIcon) {
        return;
      }
      const engine = getSearchEngine();
      const nextSrc = new URL(engine.icon, window.location.href).href;
      if (engineIcon.src !== nextSrc) {
        engineIcon.src = engine.icon;
      }
      engineIcon.alt = "";
      engineBtn.setAttribute("aria-label", `Search engine: ${engine.name}`);
      engineBtn.setAttribute("data-tooltip", engine.name);
      syncSearchBarIcon();
      if (engineMenu) {
        engineMenu.querySelectorAll(".browser-engine-option").forEach((opt) => {
          const selected = opt.dataset.engineId === engine.id;
          opt.setAttribute("aria-selected", selected ? "true" : "false");
        });
      }
    }

    function closeEngineMenu() {
      if (!engineBtn || !engineMenu || engineMenu.hidden) {
        return;
      }
      engineMenu.hidden = true;
      engineBtn.setAttribute("aria-expanded", "false");
    }

    function openEngineMenu() {
      if (!engineBtn || !engineMenu) {
        return;
      }
      if (window.OmniTooltip && typeof OmniTooltip.hide === "function") {
        OmniTooltip.hide();
      }
      engineMenu.hidden = false;
      engineBtn.setAttribute("aria-expanded", "true");
    }

    function renderEngineMenu() {
      if (!engineMenu) {
        return;
      }
      engineMenu.replaceChildren();
      listSearchEngines().forEach((engine) => {
        const opt = document.createElement("button");
        opt.type = "button";
        opt.className = "browser-engine-option";
        opt.setAttribute("role", "option");
        opt.dataset.engineId = engine.id;

        const icon = document.createElement("img");
        icon.className = "browser-engine-icon";
        icon.src = engine.icon;
        icon.alt = "";
        icon.setAttribute("aria-hidden", "true");

        const label = document.createElement("span");
        label.textContent = engine.name;

        const check = document.createElement("span");
        check.className = "browser-engine-option-check";
        check.setAttribute("aria-hidden", "true");

        opt.append(icon, label, check);
        opt.addEventListener("click", (event) => {
          event.preventDefault();
          event.stopPropagation();
          setSearchEngine(engine.id);
          syncEngineButton();
          renderEngineMenu();
          closeEngineMenu();
        });
        engineMenu.appendChild(opt);
      });
      syncEngineButton();
    }

    function bindStartClock() {
      if (!clockRoot || !clockTime || !clockPeriod) {
        return;
      }

      function paint() {
        const now = new Date();
        let hours = now.getHours();
        const minutes = now.getMinutes();
        const period = hours >= 12 ? "PM" : "AM";
        hours = hours % 12;
        if (hours === 0) {
          hours = 12;
        }
        const mm = minutes < 10 ? `0${minutes}` : String(minutes);
        clockTime.textContent = `${hours}:${mm}`;
        clockPeriod.textContent = period;
        clockRoot.setAttribute(
          "datetime",
          now.toISOString()
        );
        clockRoot.setAttribute(
          "aria-label",
          `${hours}:${mm} ${period}`
        );
      }

      paint();
      // Align ticks to the next minute boundary, then every 60s.
      const msToNextMinute =
        60000 - (Date.now() % 60000) + 20;
      window.setTimeout(() => {
        paint();
        window.setInterval(paint, 60000);
      }, msToNextMinute);
    }

    function bindEnginePicker() {
      if (!engineBtn || !engineMenu) {
        return;
      }
      renderEngineMenu();

      function toggleEngineMenu(event) {
        event.preventDefault();
        event.stopPropagation();
        if (engineMenu.hidden) {
          openEngineMenu();
        } else {
          closeEngineMenu();
        }
      }

      engineBtn.addEventListener("click", toggleEngineMenu);
      engineBtn.addEventListener("keydown", (event) => {
        if (event.key === "Enter" || event.key === " ") {
          toggleEngineMenu(event);
        }
      });

      document.addEventListener("mousedown", (event) => {
        if (engineMenu.hidden) {
          return;
        }
        if (
          engineMenu.contains(event.target) ||
          engineBtn.contains(event.target)
        ) {
          return;
        }
        closeEngineMenu();
      });

      document.addEventListener("keydown", (event) => {
        if (event.key === "Escape" && !engineMenu.hidden) {
          closeEngineMenu();
          engineBtn.focus();
        }
      });
    }

    function isBrowserOwnedUrl(url) {
      if (!url || isBlankUrl(url)) {
        return true;
      }
      return (
        isLocalSearchUrl(url) ||
        isLocalHistoryUrl(url) ||
        isLocalDownloadsUrl(url) ||
        isLocalBookmarksUrl(url) ||
        isLocalInfoUrl(url)
      );
    }

    function syncSearchBarIcon() {
      if (!searchMark) {
        return;
      }
      const tab = activeTab();
      const url = tab && tab.index >= 0 ? tab.history[tab.index] : "";
      const useBrand = !tab || isStartTab(tab) || isBrowserOwnedUrl(url);
      const src = useBrand ? NEW_TAB_FAVICON : getSearchEngine().icon;
      const nextSrc = new URL(src, window.location.href).href;
      if (searchMark.src !== nextSrc) {
        searchMark.src = src;
      }
    }

    function mountChromeIcons() {
      if (startGo) {
        OmniIcons.mount(startGo, "arrow-right");
      }
      if (btnBack) {
        OmniIcons.mount(btnBack, "arrow-left");
      }
      if (btnForward) {
        OmniIcons.mount(btnForward, "arrow-right");
      }
      if (btnReload) {
        OmniIcons.mount(btnReload, "rotate-cw");
      }
      if (btnBookmark) {
        OmniIcons.mount(btnBookmark, "bookmark");
      }
      if (btnDownloadIcon) {
        OmniIcons.mount(btnDownloadIcon, "download");
      }
      if (btnMenu) {
        OmniIcons.mount(btnMenu, "menu");
      }
      if (tabNew) {
        OmniIcons.mount(tabNew, "plus");
      }
    }

    function syncChromeHeight() {
      if (!hasNative) {
        return;
      }
      const next = measureChromeHeight();
      if (next === lastChromeHeight) {
        return;
      }
      lastChromeHeight = next;
      OmniBridge.browserSetChromeHeight(next).catch(() => {});
    }

    function syncNavButtons(state = {}) {
      if (typeof state.canGoBack === "boolean") {
        nativeCanBack = state.canGoBack;
      }
      if (typeof state.canGoForward === "boolean") {
        nativeCanForward = state.canGoForward;
      }
      if (typeof state.loading === "boolean") {
        nativeLoading = state.loading;
      }

      const browsing = root.dataset.mode === "browse";
      const tab = activeTab();
      const hasPage = Boolean(browsing && tab && tab.index >= 0);

      if (btnBack) {
        btnBack.disabled = !hasPage || !nativeCanBack;
      }
      if (btnForward) {
        btnForward.disabled = !hasPage || !nativeCanForward;
      }
      if (btnReload) {
        const loading = Boolean(state.loading ?? nativeLoading);
        const url = currentUrl() || state.url || "";
        const canReload =
          browsing && (!isBlankUrl(url) || loading || (tab && tab.index >= 0));
        btnReload.disabled = !canReload;
        btnReload.classList.toggle("is-loading", loading);
        if (loading) {
          OmniIcons.mount(btnReload, "x");
          btnReload.setAttribute("aria-label", "Stop");
          btnReload.setAttribute("data-tooltip", "Stop");
        } else {
          OmniIcons.mount(btnReload, "rotate-cw");
          btnReload.setAttribute("aria-label", "Reload");
          btnReload.setAttribute(
            "data-tooltip",
            "Reload (Shift+click: Hard Reload)"
          );
        }
      }
      syncBookmarkButton();
    }

    function setMode(mode) {
      root.dataset.mode = mode;
      const browsing = mode === "browse";
      start.hidden = browsing;
      view.hidden = !browsing;
      if (hasNative) {
        if (!browsing) {
          // Hide the content pane only — never clear other tabs' browsers
          // (that would stop background media).
          OmniBridge.browserShow(false).catch(() => {});
        }
      }
      if (window.OmniMedia && typeof OmniMedia.syncAll === "function") {
        OmniMedia.syncAll();
      }
      requestAnimationFrame(syncChromeHeight);
    }

    function syncSearchFields(value) {
      if (addressEditing && document.activeElement === searchInput) {
        return;
      }
      const display = addressBarLabel(value);
      searchInput.value = display;
      if (startInput) {
        startInput.value = display;
      }
      if (
        window.OmniAdblock &&
        typeof OmniAdblock.refresh === "function"
      ) {
        OmniAdblock.refresh().catch(() => {});
      }
    }

    function isStartTab(tab) {
      if (!tab) {
        return true;
      }
      if (tab.index < 0) {
        return true;
      }
      return isBlankUrl(tab.history[tab.index]);
    }

    function tabShowsBrandIcon(tab) {
      if (isStartTab(tab)) {
        return true;
      }
      const url = tab && tab.index >= 0 ? tab.history[tab.index] : "";
      return isLocalSearchUrl(url);
    }

    function faviconCandidatesForUrl(url) {
      try {
        const parsed = new URL(url);
        if (parsed.protocol === "file:") {
          return [NEW_TAB_FAVICON];
        }
        const host = parsed.hostname;
        const bare = host.replace(/^www\./, "");
        if (!bare) {
          return [NEW_TAB_FAVICON];
        }
        return [
          `https://www.google.com/s2/favicons?domain=${encodeURIComponent(bare)}&sz=64`,
          `https://www.google.com/s2/favicons?domain=${encodeURIComponent(host)}&sz=64`,
          `https://icons.duckduckgo.com/ip3/${bare}.ico`,
        ];
      } catch (_) {
        return [NEW_TAB_FAVICON];
      }
    }

    function bindTabFavicon(img, tab) {
      if (tabShowsBrandIcon(tab)) {
        img.src = NEW_TAB_FAVICON;
        img.removeAttribute("hidden");
        return;
      }
      const url = tab.index >= 0 ? tab.history[tab.index] : "";
      const sources = faviconCandidatesForUrl(url);
      let index = 0;
      img.decoding = "async";
      img.referrerPolicy = "no-referrer";
      img.removeAttribute("hidden");
      const tryNext = () => {
        if (index >= sources.length) {
          img.removeEventListener("error", tryNext);
          img.src = NEW_TAB_FAVICON;
          return;
        }
        img.src = sources[index];
        index += 1;
      };
      img.addEventListener("error", tryNext);
      tryNext();
    }

    function renderTabs() {
      const keepTipId = tabTipId;

      tabsEl.replaceChildren();
      tabs.forEach((tab) => {
        const btn = document.createElement("div");
        btn.className =
          "titlebar-tab" + (tab.id === activeId ? " is-active" : "");
        btn.setAttribute("role", "tab");
        btn.setAttribute("tabindex", "0");
        btn.setAttribute(
          "aria-selected",
          tab.id === activeId ? "true" : "false"
        );
        btn.dataset.tabId = tab.id;

        const url = tab.index >= 0 ? tab.history[tab.index] : "";
        const displayTitle = normalizeTabTitle(tab.title, url);

        const favicon = document.createElement("img");
        favicon.className = "titlebar-tab-favicon";
        favicon.alt = "";
        favicon.draggable = false;
        favicon.setAttribute("aria-hidden", "true");
        bindTabFavicon(favicon, tab);

        const label = document.createElement("span");
        label.className = "titlebar-tab-label";
        label.textContent = formatTabTitle(displayTitle);

        const close = document.createElement("button");
        close.type = "button";
        close.className = "titlebar-tab-close";
        close.setAttribute("aria-label", "Close tab");
        close.dataset.closeId = tab.id;

        // Explicit direct listeners on close button
        close.addEventListener("pointerdown", (e) => {
          e.preventDefault();
          e.stopPropagation();
        });
        close.addEventListener("mousedown", (e) => {
          e.preventDefault();
          e.stopPropagation();
        });
        close.addEventListener("mouseup", (e) => {
          e.preventDefault();
          e.stopPropagation();
        });
        close.addEventListener("click", (e) => {
          e.preventDefault();
          e.stopPropagation();
          hideTabHoverTip();
          closeTab(tab.id);
        });

        btn.append(favicon, label, close);

        btn.addEventListener("click", (e) => {
          if (!e.target.closest(".titlebar-tab-close, [data-close-id], .titlebar-tab-audio, [data-audio-mute]")) {
            hideTabHoverTip();
            activateTab(tab.id);
          }
        });

        if (tabAudioPlaying.get(tab.id) && !isBlankUrl(url)) {
          const muted = Boolean(tabAudioMuted.get(tab.id));
          const audio = document.createElement("button");
          audio.type = "button";
          audio.className =
            "titlebar-tab-audio" + (muted ? " is-muted" : "");
          audio.dataset.audioMute = "1";
          audio.setAttribute(
            "aria-label",
            muted ? "Unmute tab" : "Mute tab"
          );
          audio.addEventListener("pointerdown", (e) => {
            e.stopPropagation();
          });
          audio.addEventListener("click", (e) => {
            e.preventDefault();
            e.stopPropagation();
            toggleTabAudioMute(tab.id);
          });
          btn.insertBefore(audio, label);
          OmniIcons.mount(audio, "volume-2");
        }

        tabsEl.appendChild(btn);
        OmniIcons.mount(close, "x");
      });

      if (keepTipId) {
        const btn = tabsEl.querySelector(
          `.titlebar-tab[data-tab-id="${CSS.escape(keepTipId)}"]`
        );
        const tab = tabs.find((entry) => entry.id === keepTipId);
        if (btn && tab) {
          refreshOpenTabTip(btn, tab);
        } else {
          hideTabHoverTip();
        }
      }
    }

    function syncTabsOrderFromDom() {
      const order = Array.from(tabsEl.querySelectorAll(".titlebar-tab"))
        .map((btn) => btn.dataset.tabId)
        .filter(Boolean);
      const byId = new Map(tabs.map((tab) => [tab.id, tab]));
      const next = [];
      for (const id of order) {
        const tab = byId.get(id);
        if (tab) {
          next.push(tab);
          byId.delete(id);
        }
      }
      for (const tab of byId.values()) {
        next.push(tab);
      }
      tabs = next;
    }

    /** @type {{
     *   tabId: string,
     *   el: HTMLElement,
     *   pointerId: number,
     *   moved: boolean,
     *   lastScreenX: number,
     *   lastScreenY: number,
     *   ghost: HTMLElement|null,
     *   grabX: number,
     *   grabY: number,
     *   startClientX: number,
     *   startClientY: number,
     * }|null} */
    let tabDrag = null;
    let suppressTabActivate = false;
    let tabDragWatchTimer = 0;

    function stopTabDragWatch() {
      if (tabDragWatchTimer) {
        window.clearTimeout(tabDragWatchTimer);
        tabDragWatchTimer = 0;
      }
    }

    function startTabDragWatch() {
      stopTabDragWatch();
      const tick = async () => {
        tabDragWatchTimer = 0;
        if (!tabDrag) {
          return;
        }
        if (
          tabDrag.moved &&
          hasNative &&
          typeof OmniBridge.windowCursorPos === "function"
        ) {
          try {
            const pos = await OmniBridge.windowCursorPos();
            if (pos && tabDrag) {
              const sx = Number(pos.x);
              const sy = Number(pos.y);
              if (Number.isFinite(sx) && Number.isFinite(sy)) {
                tabDrag.lastScreenX = sx;
                tabDrag.lastScreenY = sy;
                positionDragGhostFromScreen(sx, sy);
                const winLeft = window.screenX || window.screenLeft || 0;
                const winTop = window.screenY || window.screenTop || 0;
                const clientX = sx - winLeft;
                const clientY = sy - winTop;
                if (pointInStrip(clientX, clientY)) {
                  placeInTabStrip(tabDrag.el, clientX);
                }
              }
              if (tabDrag && pos.primaryDown === false && tabDrag.moved) {
                endTabPointerDrag();
                return;
              }
            }
          } catch (_) {
            /* ignore */
          }
        }
        if (tabDrag) {
          tabDragWatchTimer = window.setTimeout(tick, 16);
        }
      };
      tabDragWatchTimer = window.setTimeout(tick, 16);
    }

    function removeDragGhost() {
      if (tabDrag && tabDrag.ghost) {
        tabDrag.ghost.remove();
        tabDrag.ghost = null;
      }
      document.querySelectorAll(".titlebar-tab-ghost").forEach((node) => {
        node.remove();
      });
    }

    function positionDragGhostFromScreen(screenX, screenY) {
      if (!tabDrag || !tabDrag.ghost) {
        return;
      }
      if (!Number.isFinite(screenX) || !Number.isFinite(screenY)) {
        return;
      }
      const winLeft = window.screenX || window.screenLeft || 0;
      const winTop = window.screenY || window.screenTop || 0;
      const x = screenX - winLeft - tabDrag.grabX;
      const y = screenY - winTop - tabDrag.grabY;
      tabDrag.ghost.style.transform = `translate(${x}px, ${y}px)`;
    }

    function insertPointForClientX(clientX, ignoreEl) {
      const nodes = Array.from(tabsEl.children).filter(
        (node) =>
          node.classList.contains("titlebar-tab") &&
          node !== ignoreEl
      );
      for (const node of nodes) {
        const rect = node.getBoundingClientRect();
        if (clientX < rect.left + rect.width / 2) {
          return node;
        }
      }
      return null;
    }

    function placeInTabStrip(el, clientX) {
      const before = insertPointForClientX(clientX, el);
      if (before) {
        if (el.nextElementSibling !== before) {
          tabsEl.insertBefore(el, before);
        }
        return;
      }
      if (el !== tabsEl.lastElementChild) {
        tabsEl.appendChild(el);
      }
    }

    function normalizeIncomingTab(raw) {
      if (!raw || typeof raw !== "object") {
        return null;
      }
      const history = Array.isArray(raw.history)
        ? raw.history.filter(
            (entry) => typeof entry === "string" && entry.length > 0
          )
        : [];
      let index = Number.isInteger(raw.index) ? raw.index : -1;
      if (history.length === 0) {
        index = -1;
      } else {
        index = Math.min(history.length - 1, Math.max(0, index));
      }
      const url = index >= 0 ? history[index] : "";
      return {
        title: normalizeTabTitle(
          String(raw.title || titleFromUrl(url) || "New Tab"),
          url
        ),
        history,
        index,
      };
    }

    function adoptTransferredTab(raw, { beforeId = null, activate = true } = {}) {
      const normalized = normalizeIncomingTab(raw);
      if (!normalized) {
        return null;
      }
      const tab = createTab();
      tab.title = normalized.title;
      tab.history = normalized.history;
      tab.index = normalized.index;
      let insertAt = tabs.length;
      if (beforeId) {
        const idx = tabs.findIndex((entry) => entry.id === beforeId);
        if (idx >= 0) {
          insertAt = idx;
        }
      }
      tabs.splice(insertAt, 0, tab);
      if (activate) {
        activeId = tab.id;
        applyActiveTab();
      } else {
        renderTabs();
      }
      scheduleSaveSession();
      return tab;
    }

    function stripRectExpanded() {
      const rect = tabsEl.getBoundingClientRect();
      return {
        left: rect.left - 12,
        right: rect.right + 12,
        top: rect.top - 28,
        bottom: rect.bottom + 36,
      };
    }

    function pointInStrip(clientX, clientY) {
      const box = stripRectExpanded();
      return (
        clientX >= box.left &&
        clientX <= box.right &&
        clientY >= box.top &&
        clientY <= box.bottom
      );
    }

    function cleanupTabDragChrome() {
      removeDragGhost();
      tabsEl.classList.remove("is-reordering");
      document.querySelectorAll(".titlebar-tab.is-drag-source").forEach((el) => {
        el.classList.remove("is-drag-source");
      });
    }

    function endTabPointerDrag() {
      if (!tabDrag) {
        return;
      }
      const state = tabDrag;
      tabDrag = null;
      stopTabDragWatch();
      try {
        if (
          state.el.hasPointerCapture &&
          state.el.hasPointerCapture(state.pointerId)
        ) {
          state.el.releasePointerCapture(state.pointerId);
        }
      } catch (_) {
        /* ignore */
      }
      cleanupTabDragChrome();
      if (state.moved) {
        syncTabsOrderFromDom();
        scheduleSaveSession();
        suppressTabActivate = true;
      }
    }

    function onTabPointerMove(event) {
      if (!tabDrag || event.pointerId !== tabDrag.pointerId) {
        return;
      }
      tabDrag.lastScreenX = event.screenX;
      tabDrag.lastScreenY = event.screenY;

      const dx = event.clientX - (tabDrag.startClientX || event.clientX);
      const dy = event.clientY - (tabDrag.startClientY || event.clientY);
      if (!tabDrag.moved) {
        if (Math.hypot(dx, dy) < 5) {
          return;
        }
        tabDrag.moved = true;
        hideTabHoverTip();
        tabDrag.el.classList.add("is-drag-source");
        tabsEl.classList.add("is-reordering");
        if (!tabDrag.ghost) {
          const rect = tabDrag.el.getBoundingClientRect();
          const ghost = tabDrag.el.cloneNode(true);
          ghost.classList.add("titlebar-tab-ghost", "is-active");
          ghost.classList.remove("is-drag-source");
          ghost.style.width = `${Math.round(rect.width)}px`;
          ghost.style.height = `${Math.round(rect.height)}px`;
          document.body.appendChild(ghost);
          tabDrag.ghost = ghost;
        }
        startTabDragWatch();
      }

      positionDragGhostFromScreen(event.screenX, event.screenY);
      if (pointInStrip(event.clientX, event.clientY)) {
        placeInTabStrip(tabDrag.el, event.clientX);
      }
    }

    function tabTipPayload(tab, memoryMb) {
      const url = tab && tab.index >= 0 ? tab.history[tab.index] : "";
      const title = normalizeTabTitle(tab ? tab.title : "", url);
      let memory = null;
      if (typeof memoryMb === "number" && Number.isFinite(memoryMb)) {
        memory = Math.max(0, memoryMb);
      } else if (isBlankUrl(url) || isStartTab(tab)) {
        memory = 0;
      } else if (tab && tabMemoryMb.has(tab.id)) {
        memory = tabMemoryMb.get(tab.id);
      } else if (typeof lastContentMemoryMb === "number") {
        memory = lastContentMemoryMb;
      }
      return {
        title,
        domain: domainFromUrl(url),
        audioPlaying: Boolean(tabAudioPlaying.get(tab.id)),
        audioMuted: Boolean(tabAudioMuted.get(tab.id)),
        memoryMb: memory,
      };
    }

    function tabTipKey(data) {
      const mem =
        typeof data.memoryMb === "number" && Number.isFinite(data.memoryMb)
          ? Math.round(data.memoryMb)
          : "";
      return [
        data.title || "",
        data.domain || "",
        data.audioPlaying ? "1" : "0",
        data.audioMuted ? "1" : "0",
        mem,
      ].join("\0");
    }

    function usesOverlayTabTip() {
      // Native overlay host (same stacking model as Brave's hover card).
      return (
        hasNative && typeof OmniBridge.overlayShow === "function"
      );
    }

    function presentOverlayTabTip(btn, data) {
      if (!btn || !usesOverlayTabTip()) {
        return;
      }
      const rect = btn.getBoundingClientRect();
      let left = Math.round(rect.left);
      const maxLeft = Math.max(8, window.innerWidth - TAB_TIP_WIDTH - 8);
      left = Math.max(8, Math.min(left, maxLeft));
      OmniBridge.overlayShow({
        anchorRight: left + TAB_TIP_WIDTH,
        anchorTop: Math.round(rect.bottom + 6),
        width: TAB_TIP_WIDTH,
        // 0 = keep current host size when already open (avoids shrink→grow pulse).
        height: tabTipOverlayOpen ? 0 : 120,
        payload: {
          view: "tab-tip",
          title: data.title,
          domain: data.domain || "",
          audioPlaying: Boolean(data.audioPlaying),
          audioMuted: Boolean(data.audioMuted),
          memoryMb: data.memoryMb,
        },
      }).catch(() => {});
      tabTipOverlayOpen = true;
      tabTipDataKey = tabTipKey(data);
    }

    function applyTabTipData(btn, tab, data) {
      if (!btn || !tab || tabTipId !== tab.id) {
        return;
      }
      const key = tabTipKey(data);
      if (usesOverlayTabTip()) {
        if (window.OmniTooltip && typeof OmniTooltip.hide === "function") {
          OmniTooltip.hide();
        }
        // Skip identical payloads — re-show was resizing the overlay every poll.
        if (tabTipOverlayOpen && key === tabTipDataKey) {
          return;
        }
        presentOverlayTabTip(btn, data);
        return;
      }
      if (tabTipOverlayOpen) {
        tabTipOverlayOpen = false;
        tabTipDataKey = "";
        OmniBridge.overlayHide().catch(() => {});
      }
      if (
        window.OmniTooltip &&
        typeof OmniTooltip.updateTab === "function" &&
        tabTipToken
      ) {
        if (key === tabTipDataKey) {
          return;
        }
        tabTipDataKey = key;
        OmniTooltip.updateTab(tabTipToken, btn, data);
      } else if (
        window.OmniTooltip &&
        typeof OmniTooltip.showTab === "function"
      ) {
        tabTipDataKey = key;
        tabTipToken = OmniTooltip.showTab(btn, data);
      }
    }

    function stopTabTipMemoryPoll() {
      if (tabTipMemoryPoll) {
        window.clearTimeout(tabTipMemoryPoll);
        tabTipMemoryPoll = 0;
      }
    }

    async function readTabMemoryMb(tab) {
      const url = tab && tab.index >= 0 ? tab.history[tab.index] : "";
      if (!tab || isBlankUrl(url) || isStartTab(tab)) {
        return 0;
      }
      if (!hasNative || !OmniBridge.browserState) {
        return typeof lastContentMemoryMb === "number"
          ? lastContentMemoryMb
          : null;
      }
      try {
        const state = await OmniBridge.browserState();
        if (state && typeof state.memoryMb === "number") {
          const mb = Math.max(0, state.memoryMb);
          lastContentMemoryMb = mb;
          tabMemoryMb.set(tab.id, mb);
          return mb;
        }
      } catch (_) {
        /* ignore */
      }
      if (tabMemoryMb.has(tab.id)) {
        return tabMemoryMb.get(tab.id);
      }
      return typeof lastContentMemoryMb === "number"
        ? lastContentMemoryMb
        : null;
    }

    function startTabTipMemoryPoll(btn, tab) {
      stopTabTipMemoryPoll();
      const id = tab.id;
      const tick = async () => {
        tabTipMemoryPoll = 0;
        if (tabTipId !== id || !btn.isConnected) {
          return;
        }
        const memoryMb = await readTabMemoryMb(tab);
        if (tabTipId !== id || !btn.isConnected) {
          return;
        }
        applyTabTipData(btn, tab, tabTipPayload(tab, memoryMb));
        tabTipMemoryPoll = window.setTimeout(tick, TAB_TIP_MEMORY_POLL_MS);
      };
      tabTipMemoryPoll = window.setTimeout(tick, 0);
    }

    function refreshOpenTabTip(btn, tab) {
      applyTabTipData(btn, tab, tabTipPayload(tab));
    }

    async function showTabHoverTip(btn) {
      if (!btn) {
        return;
      }
      const id = btn.dataset.tabId;
      const tab = tabs.find((entry) => entry.id === id);
      if (!tab) {
        return;
      }

      tabTipId = id;
      stopTabTipMemoryPoll();
      if (tabTipTimer) {
        window.clearTimeout(tabTipTimer);
      }

      // Prefetch RAM while the hover delay runs so the tip opens with a value.
      const prefetch = readTabMemoryMb(tab);

      tabTipTimer = window.setTimeout(async () => {
        tabTipTimer = 0;
        if (tabTipId !== id || !btn.isConnected) {
          return;
        }

        let memoryMb = null;
        try {
          memoryMb = await prefetch;
        } catch (_) {
          memoryMb = null;
        }
        if (tabTipId !== id || !btn.isConnected) {
          return;
        }

        const data = tabTipPayload(tab, memoryMb);
        if (usesOverlayTabTip()) {
          if (window.OmniTooltip && typeof OmniTooltip.hide === "function") {
            OmniTooltip.hide();
          }
          presentOverlayTabTip(btn, data);
        } else if (
          window.OmniTooltip &&
          typeof OmniTooltip.showTab === "function"
        ) {
          if (tabTipOverlayOpen) {
            tabTipOverlayOpen = false;
            OmniBridge.overlayHide().catch(() => {});
          }
          tabTipToken = OmniTooltip.showTab(btn, data);
        } else {
          return;
        }

        startTabTipMemoryPoll(btn, tab);
      }, TAB_TIP_DELAY_MS);
    }

    function hideTabHoverTip() {
      if (tabTipTimer) {
        window.clearTimeout(tabTipTimer);
        tabTipTimer = 0;
      }
      stopTabTipMemoryPoll();
      tabTipId = null;
      tabTipToken = 0;
      tabTipDataKey = "";
      if (window.OmniTooltip && typeof OmniTooltip.hide === "function") {
        OmniTooltip.hide();
      }
      if (tabTipOverlayOpen) {
        tabTipOverlayOpen = false;
        if (hasNative && typeof OmniBridge.overlayHide === "function") {
          OmniBridge.overlayHide().catch(() => {});
        }
      }
    }

    /** Clear tip state without dismissing the shared native overlay (e.g. menu takes over). */
    function releaseTabTipOverlay() {
      if (tabTipTimer) {
        window.clearTimeout(tabTipTimer);
        tabTipTimer = 0;
      }
      stopTabTipMemoryPoll();
      tabTipId = null;
      tabTipToken = 0;
      tabTipOverlayOpen = false;
      tabTipDataKey = "";
      if (window.OmniTooltip && typeof OmniTooltip.hide === "function") {
        OmniTooltip.hide();
      }
    }

    function syncActiveTabAudioIcon() {
      renderTabs();
    }

    function setTabAudioPlaying(tabId, playing) {
      if (!tabId) {
        return;
      }
      const next = Boolean(playing);
      if (next) {
        tabAudioPlaying.set(tabId, true);
      } else {
        tabAudioPlaying.delete(tabId);
      }
      if (tabId === activeId) {
        contentAudioPlaying = next;
      }
      syncActiveTabAudioIcon();
      if (tabTipId === tabId) {
        const btn = tabsEl.querySelector(
          `.titlebar-tab[data-tab-id="${CSS.escape(tabId)}"]`
        );
        const tab = tabs.find((entry) => entry.id === tabId);
        if (btn && tab) {
          refreshOpenTabTip(btn, tab);
        }
      }
    }

    function setTabAudioMuted(tabId, muted) {
      if (!tabId) {
        return;
      }
      const next = Boolean(muted);
      if (next) {
        tabAudioMuted.set(tabId, true);
      } else {
        tabAudioMuted.delete(tabId);
      }
      if (tabId === activeId) {
        contentAudioMuted = next;
      }
      syncActiveTabAudioIcon();
      if (tabTipId === tabId) {
        const btn = tabsEl.querySelector(
          `.titlebar-tab[data-tab-id="${CSS.escape(tabId)}"]`
        );
        const tab = tabs.find((entry) => entry.id === tabId);
        if (btn && tab) {
          refreshOpenTabTip(btn, tab);
        }
      }
    }

    function setContentAudioPlaying(playing) {
      setTabAudioPlaying(activeId, playing);
    }

    function setContentAudioMuted(muted) {
      setTabAudioMuted(activeId, muted);
    }

    async function toggleTabAudioMute(tabId) {
      if (!hasNative || typeof OmniBridge.browserSetAudioMuted !== "function") {
        return;
      }
      const id = tabId || activeId;
      if (!id) {
        return;
      }
      const next = !Boolean(tabAudioMuted.get(id));
      try {
        const result = await OmniBridge.browserSetAudioMuted(next, id);
        if (result && typeof result.muted === "boolean") {
          setTabAudioMuted(id, result.muted);
        } else {
          setTabAudioMuted(id, next);
        }
      } catch (_) {
        setTabAudioMuted(id, next);
      }
      if (tabTipId === id) {
        const btn = tabsEl.querySelector(
          `.titlebar-tab[data-tab-id="${CSS.escape(id)}"]`
        );
        const tab = tabs.find((entry) => entry.id === id);
        if (btn && tab) {
          refreshOpenTabTip(btn, tab);
        }
      }
    }

    function recordHistory(url, mode) {
      const tab = activeTab();
      if (!tab || !url || url === "about:blank") {
        return;
      }

      if (mode === "traverse") {
        const existing = tab.history.findIndex((entry) => urlsMatch(entry, url));
        if (existing >= 0) {
          tab.index = existing;
        } else if (tab.index >= 0) {
          tab.history[tab.index] = url;
        } else {
          tab.history = [url];
          tab.index = 0;
        }
        lastPushedUrl = url;
        rememberVisit(url, tab.title);
        return;
      }

      if (tab.index >= 0 && urlsMatch(tab.history[tab.index], url)) {
        tab.history[tab.index] = url;
        lastPushedUrl = url;
        rememberVisit(url, tab.title);
        return;
      }
      if (urlsMatch(lastPushedUrl, url) && tab.index >= 0) {
        return;
      }

      tab.history = tab.history.slice(0, tab.index + 1);
      tab.history.push(url);
      tab.index = tab.history.length - 1;
      lastPushedUrl = url;
      rememberVisit(url, tab.title);
    }

    function ensureNativeTabs() {
      if (!hasNative || typeof OmniBridge.browserEnsureTab !== "function") {
        return;
      }
      // Start-page tabs have no content URL. Creating a CEF view for them at
      // boot, then closing the extra New Tab, recycles a still-booting Alloy
      // browser and crashes when Wikipedia (or any restored page) navigates.
      for (const tab of tabs) {
        if (isStartTab(tab)) {
          continue;
        }
        OmniBridge.browserEnsureTab(tab.id).catch(() => {});
      }
    }

    function applyActiveTab() {
      const tab = activeTab();
      if (!tab) {
        return;
      }
      pendingTraverse = 0;
      nativeCanBack = false;
      nativeCanForward = false;
      const url = tab.index >= 0 ? tab.history[tab.index] : "";
      if (url && !isBlankUrl(url)) {
        lastPushedUrl = url;
        if (hasNative) {
          const run = async () => {
            if (typeof OmniBridge.browserEnsureTab === "function") {
              await OmniBridge.browserEnsureTab(tab.id);
            }
            if (typeof OmniBridge.browserActivateTab === "function") {
              await OmniBridge.browserActivateTab(tab.id);
            }
            if (!tab.contentLive) {
              tab.contentLive = true;
              await OmniBridge.browserNavigate(url, tab.id);
            }
          };
          run().catch(() => {});
        }
        syncSearchFields(url);
        root.dataset.mode = "browse";
        start.hidden = true;
        view.hidden = false;
        requestAnimationFrame(syncChromeHeight);
      } else {
        lastPushedUrl = "";
        if (hasNative && typeof OmniBridge.browserActivateTab === "function") {
          OmniBridge.browserActivateTab(tab.id).catch(() => {});
        }
        syncSearchFields("");
        setMode("start");
      }
      contentAudioPlaying = Boolean(tabAudioPlaying.get(tab.id));
      contentAudioMuted = Boolean(tabAudioMuted.get(tab.id));
      if (window.OmniMedia && typeof OmniMedia.syncAll === "function") {
        OmniMedia.syncAll();
      }
      syncNavButtons();
      syncSearchBarIcon();
      if (aiPill) {
        aiPill.hidden = true;
      }
      renderTabs();
      scheduleSaveSession();
    }

    function loadUrl(url, { push = true } = {}) {
      const tab = activeTab();
      if (!tab || !url) {
        return;
      }
      const previousUrl = tab.index >= 0 ? tab.history[tab.index] : "";
      const serial = ++navSerial;
      pendingTraverse = 0;
      if (push) {
        recordHistory(url, "push");
      } else {
        lastPushedUrl = url;
      }
      setProvisionalTitle(tab, url, previousUrl);
      tab.contentLive = true;
      syncSearchFields(url);
      syncSearchBarIcon();
      root.dataset.mode = "browse";
      start.hidden = true;
      view.hidden = false;
      syncNavButtons({ loading: true });
      renderTabs();
      scheduleSaveSession();
      requestAnimationFrame(syncChromeHeight);
      if (!hasNative) {
        console.error("Native content browser unavailable");
        return;
      }
      const go = async () => {
        if (typeof OmniBridge.browserEnsureTab === "function") {
          await OmniBridge.browserEnsureTab(tab.id);
        }
        if (typeof OmniBridge.browserActivateTab === "function") {
          await OmniBridge.browserActivateTab(tab.id);
        }
        if (serial !== navSerial) {
          return;
        }
        await OmniBridge.browserNavigate(url, tab.id);
      };
      go().catch((err) => {
        console.error("browser.navigate failed", err);
      });
    }

    function navigate(raw) {
      const typed = String(raw || "").trim();
      const current = currentUrl();
      if (typed && typed === addressBarLabel(current) && isBrowserOwnedUrl(current)) {
        addressEditing = false;
        syncSearchFields(current);
        return;
      }
      const url = toNavigationUrl(raw);
      if (!url) {
        return;
      }
      addressEditing = false;
      loadUrl(url, { push: true });
    }

    function goBack() {
      if (!hasNative || !nativeCanBack) {
        return;
      }
      pendingTraverse += 1;
      syncNavButtons({ loading: true });
      OmniBridge.browserStop()
        .catch(() => {})
        .finally(() => {
          OmniBridge.browserBack().catch(() => {
            pendingTraverse = Math.max(0, pendingTraverse - 1);
            syncNavButtons();
          });
        });
    }

    function goForward() {
      if (!hasNative || !nativeCanForward) {
        return;
      }
      pendingTraverse += 1;
      syncNavButtons({ loading: true });
      OmniBridge.browserStop()
        .catch(() => {})
        .finally(() => {
          OmniBridge.browserForward().catch(() => {
            pendingTraverse = Math.max(0, pendingTraverse - 1);
            syncNavButtons();
          });
        });
    }

    function reloadPage({ ignoreCache = false } = {}) {
      if (root.dataset.mode !== "browse") {
        return;
      }
      const tab = activeTab();
      const url = currentUrl();
      if ((!tab || tab.index < 0) && isBlankUrl(url)) {
        return;
      }
      if (!hasNative) {
        if (!isBlankUrl(url)) {
          loadUrl(url, { push: false });
        }
        return;
      }
      if (nativeLoading && !ignoreCache) {
        OmniBridge.browserStop().catch(() => {});
        return;
      }
      OmniBridge.browserReload(Boolean(ignoreCache)).catch(() => {});
    }

    function goHome() {
      const tab = activeTab();
      if (!tab) {
        return;
      }
      pendingTraverse = 0;
      navSerial += 1;
      nativeCanBack = false;
      nativeCanForward = false;
      lastPushedUrl = "";
      tab.history = [];
      tab.index = -1;
      tab.title = "New Tab";
      tab.contentLive = false;
      tabAudioPlaying.delete(tab.id);
      tabAudioMuted.delete(tab.id);
      if (window.OmniMedia) {
        OmniMedia.clearTab(tab.id);
      }
      // Keep the native content browser for this tab — closing+recreating it
      // on the next navigation races CEF and can crash the app.
      if (hasNative) {
        if (typeof OmniBridge.browserClear === "function") {
          OmniBridge.browserClear().catch(() => {});
        }
        if (typeof OmniBridge.browserShow === "function") {
          OmniBridge.browserShow(false).catch(() => {});
        }
        if (typeof OmniBridge.browserActivateTab === "function") {
          OmniBridge.browserActivateTab(tab.id).catch(() => {});
        }
      }
      syncSearchFields("");
      setMode("start");
      syncNavButtons();
      renderTabs();
      scheduleSaveSession();
      requestAnimationFrame(() => {
        if (startInput) {
          startInput.focus();
        } else {
          searchInput.focus();
        }
      });
    }

    function openTab(initialUrl) {
      const tab = createTab();
      tabs.push(tab);
      activeId = tab.id;
      applyActiveTab();
      scheduleSaveSession();
      // Click handlers pass a PointerEvent; only real string URLs count.
      const raw =
        typeof initialUrl === "string" ? initialUrl.trim() : "";
      if (raw) {
        const url = toNavigationUrl(raw) || raw;
        addressEditing = false;
        loadUrl(url, { push: true });
        return;
      }
      requestAnimationFrame(() => {
        if (startInput) {
          startInput.focus();
        } else {
          searchInput.focus();
        }
      });
    }

    function activateTab(id) {
      if (!tabs.some((tab) => tab.id === id) || activeId === id) {
        return;
      }
      activeId = id;
      applyActiveTab();
      scheduleSaveSession();
    }

    function closeTab(id) {
      console.log("[OmniBrowser] closeTab requested for id:", id);
      const index = tabs.findIndex((tab) => tab.id === id);
      if (index < 0) {
        console.warn("[OmniBrowser] closeTab: id not found in tabs list:", id, tabs);
        renderTabs();
        return;
      }
      const closing = tabs[index];
      const closeUrl =
        closing.index >= 0 ? closing.history[closing.index] : "";
      if (closing.history && closing.history.length > 0 && !isBlankUrl(closeUrl)) {
        closedTabs.unshift({
          id: `closed-${Date.now()}-${Math.random().toString(36).slice(2, 7)}`,
          title: closing.title || titleFromUrl(closeUrl),
          history: closing.history.slice(),
          index: closing.index,
          ts: Date.now(),
        });
        if (closedTabs.length > MAX_CLOSED_TABS) {
          closedTabs.length = MAX_CLOSED_TABS;
        }
        saveClosedTabs();
        rememberVisit(closeUrl, closing.title);
      }
      if (hasNative && typeof OmniBridge.browserCloseTab === "function") {
        OmniBridge.browserCloseTab(id).catch((err) => {
          console.error("[OmniBrowser] browserCloseTab IPC error:", err);
        });
      }
      tabAudioPlaying.delete(id);
      tabAudioMuted.delete(id);
      if (window.OmniMedia) {
        OmniMedia.clearTab(id);
      }
      tabs.splice(index, 1);
      if (tabs.length === 0) {
        openTab();
        return;
      }
      if (activeId === id) {
        const next = tabs[index] || tabs[index - 1];
        activeId = next.id;
        applyActiveTab();
      } else {
        renderTabs();
      }
      scheduleSaveSession();
    }

    function listHistory() {
      return visitHistory.map((entry) => ({
        url: entry.url,
        title: entry.title,
        ts: entry.ts,
      }));
    }

    function listRecentTabs() {
      return closedTabs.map((entry) => ({
        id: entry.id,
        title: entry.title,
        history: entry.history,
        index: entry.index,
        ts: entry.ts,
      }));
    }

    function setDownloadProgress(percent, { active = 0, complete = false } = {}) {
      if (!btnDownload || !btnDownloadProgress) {
        return;
      }
      const pct = Math.max(0, Math.min(100, Number(percent) || 0));
      const offset = DOWNLOAD_RING * (1 - pct / 100);
      btnDownloadProgress.style.strokeDasharray = String(DOWNLOAD_RING);
      btnDownloadProgress.style.strokeDashoffset = String(offset);
      btnDownload.classList.toggle("is-complete", Boolean(complete) && active <= 0);
      btnDownload.setAttribute("aria-valuenow", String(Math.round(pct)));
    }

    function showDownloadButton() {
      if (!btnDownload) {
        return;
      }
      if (downloadHideTimer) {
        window.clearTimeout(downloadHideTimer);
        downloadHideTimer = 0;
      }
      btnDownload.hidden = false;
    }

    function scheduleHideDownloadButton() {
      if (!btnDownload) {
        return;
      }
      if (downloadHideTimer) {
        window.clearTimeout(downloadHideTimer);
      }
      downloadHideTimer = window.setTimeout(() => {
        downloadHideTimer = 0;
        btnDownload.hidden = true;
        btnDownload.classList.remove("is-complete");
        setDownloadProgress(0, { active: 0 });
      }, 2800);
    }

    function openDownloadsPage() {
      const downloadsUrl =
        typeof localDownloadsUrl === "function"
          ? localDownloadsUrl()
          : new URL("downloads.html", window.location.href).href;
      openTab(downloadsUrl);
    }

    function onDownloadEvent(msg) {
      if (!msg || msg.type !== "download") {
        return;
      }
      const active = Number(msg.active) || 0;
      const percent = Number(msg.percent) || 0;
      if (active > 0) {
        showDownloadButton();
        setDownloadProgress(percent, { active, complete: false });
        return;
      }
      // Finished: flash complete, then hide.
      showDownloadButton();
      setDownloadProgress(100, { active: 0, complete: true });
      scheduleHideDownloadButton();
    }

    function onBrowserEvent(msg) {
      if (!msg || !msg.type) {
        return;
      }
      if (msg.type === "download") {
        onDownloadEvent(msg);
        return;
      }
      if (msg.type === "ai.active" || msg.type === "ai.status") {
        const isActive = Boolean(msg.active);
        if (isActive) {
          aiPaused = false;
          document.body.classList.add("ai-active");
          if (root) root.classList.add("ai-active");
          if (aiPill) {
            const text = aiPill.querySelector(".ai-control-pill-text");
            const count = Number(msg.agentCount) || 0;
            if (text) {
              text.textContent = count > 1 ? count + " Agents" : "Agent Controlled";
            }
            aiPill.hidden = true;
          }
          if (aiActiveTimer) {
            window.clearTimeout(aiActiveTimer);
            aiActiveTimer = 0;
          }
        } else {
          if (aiActiveTimer) {
            window.clearTimeout(aiActiveTimer);
            aiActiveTimer = 0;
          }
          document.body.classList.remove("ai-active");
          if (root) root.classList.remove("ai-active");
          if (aiPill) aiPill.hidden = true;
        }
        return;
      }

      if (msg.type === "tab.created") {
        const tabId = msg.tabId;
        if (tabId) {
          let existing = tabs.find((t) => t.id === tabId);
          if (!existing) {
            const url = msg.url || "";
            const isBlank = !url || url === "about:blank";
            existing = {
              id: tabId,
              title: msg.title || (url && !isBlank ? titleFromUrl(url) : "New Tab"),
              history: url && !isBlank ? [url] : [],
              index: url && !isBlank ? 0 : -1,
              contentLive: true,
            };
            tabs.push(existing);
          }
          if (msg.activate) {
            activeId = tabId;
            applyActiveTab();
          }
          renderTabs();
          scheduleSaveSession();
        }
        return;
      }

      if (msg.type === "tab.activated") {
        const tabId = msg.tabId;
        if (tabId && tabs.some((t) => t.id === tabId)) {
          activeId = tabId;
          applyActiveTab();
          renderTabs();
          syncSearchFields(currentUrl());
          syncBookmarkButton();
        }
        return;
      }

      if (msg.type === "tab.closed") {
        const tabId = msg.tabId;
        const at = tabs.findIndex((t) => t.id === tabId);
        if (at >= 0) {
          tabs.splice(at, 1);
          if (!tabs.length) {
            const newT = createTab();
            tabs.push(newT);
            activeId = newT.id;
            applyActiveTab();
          } else if (activeId === tabId) {
            activeId = tabs[Math.min(at, tabs.length - 1)].id;
            applyActiveTab();
          } else {
            renderTabs();
          }
          scheduleSaveSession();
        }
        return;
      }

      const eventTabId =
        typeof msg.tabId === "string" && msg.tabId ? msg.tabId : "";
      const tab = eventTabId
        ? tabs.find((entry) => entry.id === eventTabId)
        : activeTab();
      if (eventTabId && !tab) {
        return;
      }
      const isActiveEvent = !eventTabId || eventTabId === activeId;

      if (msg.type === "navigate" || msg.type === "visibility") {
        if (isActiveEvent) {
          if (msg.visible) {
            root.dataset.mode = "browse";
            start.hidden = true;
            view.hidden = false;
            requestAnimationFrame(syncChromeHeight);
          } else if (msg.type === "visibility") {
            // Only flip chrome to start when the active tab has no content.
            const active = activeTab();
            if (active && isStartTab(active)) {
              root.dataset.mode = "start";
              start.hidden = false;
              view.hidden = true;
              requestAnimationFrame(syncChromeHeight);
            }
          }
        }
        if (msg.url && tab && msg.url !== "about:blank") {
          const previousUrl = tab.index >= 0 ? tab.history[tab.index] : "";
          const mode = pendingTraverse > 0 ? "traverse" : "push";
          if (isActiveEvent) {
            recordHistory(msg.url, mode);
          }
          setProvisionalTitle(tab, msg.url, previousUrl);
          if (isActiveEvent) {
            syncSearchFields(msg.url);
          }
          renderTabs();
          scheduleSaveSession();
        }
        if (isActiveEvent) {
          syncNavButtons(msg);
        }
      }

      if (msg.type === "address") {
        const url = msg.url || "";
        if (tab && url && url !== "about:blank") {
          const previousUrl = tab.index >= 0 ? tab.history[tab.index] : "";
          if (isActiveEvent) {
            const traversing = pendingTraverse > 0;
            if (traversing) {
              pendingTraverse = Math.max(0, pendingTraverse - 1);
            }
            recordHistory(url, traversing ? "traverse" : "push");
            syncSearchFields(url);
          }
          setProvisionalTitle(tab, url, previousUrl);
          renderTabs();
          scheduleSaveSession();
        }
        if (isActiveEvent) {
          syncNavButtons(msg);
        }
      }

      if (msg.type === "loading") {
        if (msg.url && tab && msg.url !== "about:blank" && !msg.loading) {
          if (tab.index >= 0 && !urlsMatch(tab.history[tab.index], msg.url)) {
            const previousUrl = tab.index >= 0 ? tab.history[tab.index] : "";
            if (isActiveEvent) {
              const mode = pendingTraverse > 0 ? "traverse" : "push";
              if (pendingTraverse > 0) {
                pendingTraverse = Math.max(0, pendingTraverse - 1);
              }
              recordHistory(msg.url, mode);
              syncSearchFields(msg.url);
            }
            setProvisionalTitle(tab, msg.url, previousUrl);
            renderTabs();
            scheduleSaveSession();
          }
        }
        if (isActiveEvent) {
          syncNavButtons(msg);
        }
      }

      if (msg.type === "title" && tab && msg.title) {
        const url =
          tab.index >= 0 ? tab.history[tab.index] || "" : currentUrl();
        if (isBlankUrl(msg.title) || /^about:blank$/i.test(String(msg.title))) {
          if (isStartTab(tab) || isBlankUrl(url)) {
            if (!tab.title || tab.title === "New Tab") {
              tab.title = "New Tab";
              renderTabs();
              scheduleSaveSession();
            }
          }
          return;
        }
        if (isStartTab(tab)) {
          tab.title = "New Tab";
        } else {
          tab.title = normalizeTabTitle(msg.title, url);
          if (isActiveEvent) {
            rememberVisit(url, tab.title);
          }
        }
        renderTabs();
        scheduleSaveSession();
      }

      if (msg.type === "audio") {
        const audioTabId = eventTabId || activeId;
        if (typeof msg.playing === "boolean") {
          setTabAudioPlaying(audioTabId, msg.playing);
        }
        if (typeof msg.muted === "boolean") {
          setTabAudioMuted(audioTabId, msg.muted);
        }
      }

      if (msg.type === "media") {
        if (window.OmniMedia) {
          OmniMedia.onMediaEvent(msg);
        }
        const audioTabId = eventTabId || activeId;
        if (typeof msg.playing === "boolean") {
          setTabAudioPlaying(audioTabId, Boolean(msg.playing));
        }
      }

      if (msg.type === "bookmarks") {
        pullBookmarksFromNative();
      }

      if (msg.type === "overlay" && msg.visible === false) {
        tabTipOverlayOpen = false;
        tabTipDataKey = "";
        if (window.OmniOverlayManager) {
          OmniOverlayManager.onNativeDismissAll();
        }
        if (tabTipId && usesOverlayTabTip()) {
          tabTipId = null;
          if (tabTipTimer) {
            window.clearTimeout(tabTipTimer);
            tabTipTimer = 0;
          }
        }
      }

      if (isActiveEvent) {
        syncSearchBarIcon();
      }
    }

    function bindNativeEvents() {
      if (!hasNative || !OmniBridge.browserSubscribe) {
        return;
      }
      OmniBridge.browserSubscribe((msg, err) => {
        if (err) {
          return;
        }
        onBrowserEvent(msg);
      }).catch(() => {});
    }

    function bindUiEvents() {
      searchForm.addEventListener("submit", (event) => {
        event.preventDefault();
        navigate(searchInput.value);
      });

      searchInput.addEventListener("focus", () => {
        addressEditing = true;
      });
      searchInput.addEventListener("blur", () => {
        addressEditing = false;
        syncSearchFields(currentUrl());
      });

      if (startForm && startInput) {
        startForm.addEventListener("submit", (event) => {
          event.preventDefault();
          navigate(startInput.value);
        });
      }

      if (btnBack) {
        btnBack.addEventListener("click", (event) => {
          event.preventDefault();
          goBack();
        });
      }

      if (btnForward) {
        btnForward.addEventListener("click", (event) => {
          event.preventDefault();
          goForward();
        });
      }

      if (btnReload) {
        btnReload.addEventListener("click", (event) => {
          event.preventDefault();
          reloadPage({
            ignoreCache: Boolean(event.shiftKey),
          });
        });
      }

      if (btnDownload) {
        btnDownload.addEventListener("click", (event) => {
          event.preventDefault();
          openDownloadsPage();
        });
      }

      window.addEventListener("keydown", (event) => {
        const ctrl = event.ctrlKey || event.metaKey;
        if (ctrl && event.shiftKey && (event.key === "T" || event.key === "t")) {
          event.preventDefault();
          restoreClosedTab();
          return;
        }
        if (root.dataset.mode !== "browse") {
          return;
        }
        const key = event.key;
        const isF5 = key === "F5";
        const isR = key === "r" || key === "R";
        if (!isF5 && !(ctrl && isR)) {
          return;
        }
        event.preventDefault();
        const hard = isF5
          ? Boolean(event.shiftKey || event.ctrlKey)
          : Boolean(event.shiftKey);
        reloadPage({ ignoreCache: hard });
      });

      if (btnBookmark) {
        btnBookmark.addEventListener("click", () => {
          const url = currentUrl();
          if (!url) {
            return;
          }
          if (isBookmarked(url)) {
            removeBookmark(url);
          } else {
            const tab = activeTab();
            addBookmark(url, tab ? tab.title : titleFromUrl(url));
          }
          syncBookmarkButton();
        });
      }

      if (tabNew) {
        tabNew.addEventListener("click", openTab);
      }

      tabsEl.addEventListener("pointerdown", (event) => {
        if (event.button !== 0) {
          return;
        }
        if (
          event.target.closest(
            "[data-close-id], .titlebar-tab-close, [data-audio-mute], .titlebar-tab-audio"
          )
        ) {
          return;
        }
        const tabBtn = event.target.closest(".titlebar-tab");
        if (!tabBtn || !tabsEl.contains(tabBtn)) {
          return;
        }
        const tabId = tabBtn.dataset.tabId;
        const tab = tabs.find((entry) => entry.id === tabId);
        if (!tab) {
          return;
        }

        hideTabHoverTip();
        const rect = tabBtn.getBoundingClientRect();
        const grabX = Math.max(0, event.clientX - rect.left);
        const grabY = Math.max(0, event.clientY - rect.top);

        tabDrag = {
          tabId,
          el: tabBtn,
          pointerId: event.pointerId,
          moved: false,
          lastScreenX: event.screenX,
          lastScreenY: event.screenY,
          ghost: null,
          grabX,
          grabY,
          startClientX: event.clientX,
          startClientY: event.clientY,
        };

        try {
          tabBtn.setPointerCapture(event.pointerId);
        } catch (_) {
          /* ignore */
        }

        if (activeId !== tabId) {
          activateTab(tabId);
        }
      });

      tabsEl.addEventListener("pointermove", onTabPointerMove);

      tabsEl.addEventListener("pointerup", (event) => {
        if (!tabDrag || event.pointerId !== tabDrag.pointerId) {
          return;
        }
        tabDrag.lastScreenX = event.screenX;
        tabDrag.lastScreenY = event.screenY;
        endTabPointerDrag();
      });

      tabsEl.addEventListener("pointercancel", (event) => {
        if (!tabDrag || event.pointerId !== tabDrag.pointerId) {
          return;
        }
        endTabPointerDrag();
      });

      tabsEl.addEventListener("lostpointercapture", () => {
        if (!tabDrag) {
          return;
        }
        // Capture often drops when leaving the shell CEF view; keep tracking
        // via native cursor polling if the drag already started.
        if (tabDrag.moved) {
          startTabDragWatch();
          return;
        }
        endTabPointerDrag();
      });

      tabsEl.addEventListener("pointerover", (event) => {
        if (tabDrag) {
          return;
        }
        const tabBtn = event.target.closest(".titlebar-tab");
        if (!tabBtn || !tabsEl.contains(tabBtn)) {
          return;
        }
        if (tabTipId === tabBtn.dataset.tabId) {
          return;
        }
        showTabHoverTip(tabBtn);
      });
      tabsEl.addEventListener("pointerout", (event) => {
        const tabBtn = event.target.closest(".titlebar-tab");
        if (!tabBtn || !tabsEl.contains(tabBtn)) {
          return;
        }
        const related = event.relatedTarget;
        if (related && tabBtn.contains(related)) {
          return;
        }
        if (tabTipId === tabBtn.dataset.tabId) {
          hideTabHoverTip();
        }
      });

      tabsEl.addEventListener("click", (event) => {
        const audioBtn = event.target.closest("[data-audio-mute]");
        if (audioBtn && tabsEl.contains(audioBtn)) {
          event.preventDefault();
          event.stopPropagation();
          const tabBtn = audioBtn.closest(".titlebar-tab");
          toggleTabAudioMute(tabBtn && tabBtn.dataset.tabId);
          return;
        }
        const closeBtn = event.target.closest("[data-close-id], .titlebar-tab-close");
        if (closeBtn) {
          event.preventDefault();
          event.stopPropagation();
          hideTabHoverTip();
          const tabId =
            closeBtn.getAttribute("data-close-id") ||
            closeBtn.dataset.closeId ||
            (closeBtn.closest(".titlebar-tab")
              ? closeBtn.closest(".titlebar-tab").dataset.tabId
              : "");
          if (tabId) {
            closeTab(tabId);
          }
          return;
        }
        if (suppressTabActivate) {
          suppressTabActivate = false;
          event.preventDefault();
          return;
        }
        const tabBtn = event.target.closest(".titlebar-tab");
        if (tabBtn && tabBtn.dataset.tabId) {
          hideTabHoverTip();
          activateTab(tabBtn.dataset.tabId);
        }
      });

      tabsEl.addEventListener("auxclick", (event) => {
        if (event.button === 1) {
          event.preventDefault();
          event.stopPropagation();
          hideTabHoverTip();
          const tabBtn = event.target.closest(".titlebar-tab");
          if (tabBtn && tabBtn.dataset.tabId) {
            closeTab(tabBtn.dataset.tabId);
          }
        }
      });

      window.addEventListener("resize", syncChromeHeight);
      window.addEventListener("pagehide", saveSession);
      window.addEventListener("beforeunload", saveSession);
    }

    mountChromeIcons();
    bindStartClock();
    bindEnginePicker();
    bindUiEvents();
    syncChromeHeight();
    bindNativeEvents();
    migrateVisitHistoryToNative();
    migrateBookmarksToNative();
    pullVisitHistoryFromNative();
    pullBookmarksFromNative();
    document.addEventListener("visibilitychange", () => {
      if (document.visibilityState === "visible") {
        pullVisitHistoryFromNative();
        pullBookmarksFromNative();
      }
    });
    if (!usesNativeStore()) {
      window.addEventListener("storage", (event) => {
        if (event && event.key === BOOKMARKS_KEY) {
          bookmarkEntries = loadBookmarks();
          syncBookmarkButton();
        }
      });
    }

    const bootPending =
      hasNative && typeof OmniBridge.tabConsumePending === "function"
        ? OmniBridge.tabConsumePending().catch(() => null)
        : Promise.resolve(null);

    bootPending.then((pending) => {
      if (pending && pending.tab) {
        const hadSession = restoreSession();
        if (!hadSession) {
          tabs = [];
          activeId = "";
        }
        adoptTransferredTab(pending.tab, { activate: true });
        ensureNativeTabs();
        syncSessionToNative(buildSessionPayload());
        return;
      }
      const restoredLocal = restoreSession();
      const finishBoot = () => {
        if (tabs.length > 0) {
          ensureNativeTabs();
          syncSessionToNative(buildSessionPayload());
          const restoredActiveId = activeId;
          window.setTimeout(() => {
            if (activeId === restoredActiveId) {
              applyActiveTab();
            }
          }, 150);
          return;
        }
        openTab();
      };
      if (restoredLocal) {
        finishBoot();
        return;
      }
      restoreSessionFromNative().then((restoredNative) => {
        if (!restoredNative) {
          openTab();
          return;
        }
        finishBoot();
      });
    });

    window.OmniBrowser = {
      navigate,
      goHome,
      openTab,
      activateTab,
      closeTab,
      syncChromeHeight,
      getCurrentUrl: currentUrl,
      releaseTabTip: releaseTabTipOverlay,
      listBookmarks() {
        return bookmarkEntries.map((entry) => ({
          url: entry.url,
          title: entry.title,
          ts: entry.ts,
        }));
      },
      listHistory,
      listRecentTabs,
      restoreClosedTab,
      focusSearch() {
        const tab = activeTab();
        if ((!tab || tab.index < 0) && startInput) {
          startInput.focus();
          startInput.select();
          return;
        }
        searchInput.focus();
        searchInput.select();
      },
    };
  }

  window.OmniBrowserBoot = { bootBrowser };
})();
