(() => {
  const STORAGE_KEY = "omni.searchEngine";

  const ENGINES = [
    {
      id: "qubrain",
      name: "QuBrain Search",
      queryUrl: null,
      local: true,
      icon: "assets/qubrain.svg",
    },
    {
      id: "brave",
      name: "Brave",
      queryUrl: "https://search.brave.com/search?q=",
      icon: "assets/QuBrain-icons/brave-color.svg",
    },
    {
      id: "duckduckgo",
      name: "DuckDuckGo",
      queryUrl: "https://duckduckgo.com/?q=",
      icon: "assets/QuBrain-icons/duckduckgo-color.svg",
    },
    {
      id: "qwant",
      name: "Qwant",
      queryUrl: "https://www.qwant.com/?q=",
      icon: "assets/QuBrain-icons/qwant-color.svg",
    },
    {
      id: "google",
      name: "Google",
      queryUrl: "https://www.google.com/search?q=",
      icon: "assets/QuBrain-icons/google-color.svg",
    },
    {
      id: "bing",
      name: "Bing",
      queryUrl: "https://www.bing.com/search?q=",
      icon: "assets/QuBrain-icons/bing-color.svg",
    },
    {
      id: "startpage",
      name: "Startpage",
      queryUrl: "https://www.startpage.com/sp/search?query=",
      icon: "assets/QuBrain-icons/startpage-color.svg",
    },
    {
      id: "ecosia",
      name: "Ecosia",
      queryUrl: "https://www.ecosia.org/search?q=",
      icon: "assets/QuBrain-icons/ecosia-color.svg",
    },
  ];

  const DEFAULT_ENGINE_ID = "qubrain";

  function readStoredId() {
    try {
      return localStorage.getItem(STORAGE_KEY) || DEFAULT_ENGINE_ID;
    } catch (_) {
      return DEFAULT_ENGINE_ID;
    }
  }

  let activeId = readStoredId();
  if (!ENGINES.some((engine) => engine.id === activeId)) {
    activeId = DEFAULT_ENGINE_ID;
  }

  function getEngine(id) {
    const fallback =
      ENGINES.find((engine) => engine.id === DEFAULT_ENGINE_ID) || ENGINES[0];
    return ENGINES.find((engine) => engine.id === (id || activeId)) || fallback;
  }

  function getSearchEngine() {
    return getEngine(activeId);
  }

  function setSearchEngine(id) {
    const engine = getEngine(id);
    activeId = engine.id;
    try {
      localStorage.setItem(STORAGE_KEY, activeId);
    } catch (_) {
      /* ignore quota / private mode */
    }
    if (
      window.OmniBridge &&
      typeof OmniBridge.settingsSet === "function"
    ) {
      OmniBridge.settingsSet({ key: STORAGE_KEY, value: activeId }).catch(
        () => {}
      );
    }
    return engine;
  }

  function pullSearchEngineFromSettings() {
    if (
      !window.OmniBridge ||
      typeof OmniBridge.settingsGet !== "function"
    ) {
      return;
    }
    OmniBridge.settingsGet({ key: STORAGE_KEY })
      .then((result) => {
        const value = result && result.value;
        if (typeof value === "string" && ENGINES.some((e) => e.id === value)) {
          activeId = value;
        }
      })
      .catch(() => {});
  }

  pullSearchEngineFromSettings();

  function listSearchEngines() {
    // Selected engine always first in the menu.
    const selected = getSearchEngine();
    return [selected, ...ENGINES.filter((engine) => engine.id !== selected.id)];
  }

  function looksLikeUrl(value) {
    const v = String(value || "").trim();
    if (!v) {
      return false;
    }
    if (/^[a-z][a-z0-9+.-]*:\/\//i.test(v)) {
      return true;
    }
    if (v.startsWith("localhost") || /^localhost[:/]/i.test(v)) {
      return true;
    }
    if (/^(\d{1,3}\.){3}\d{1,3}(:\d+)?(\/|$)/.test(v)) {
      return true;
    }
    if (/^[a-z0-9-]+(\.[a-z0-9-]+)+([/:?#].*)?$/i.test(v) && !/\s/.test(v)) {
      return true;
    }
    return false;
  }

  function localSearchUrl(query) {
    const url = new URL("search.html", window.location.href);
    url.searchParams.set("q", query);
    return url.href;
  }

  function isLocalSearchUrl(url) {
    try {
      const parsed = new URL(url);
      const path = parsed.pathname.replace(/\\/g, "/").toLowerCase();
      return path.endsWith("/search.html") || path.endsWith("search.html");
    } catch (_) {
      return false;
    }
  }

  function localHistoryUrl() {
    return new URL("history.html", window.location.href).href;
  }

  function isLocalHistoryUrl(url) {
    try {
      const parsed = new URL(url);
      const path = parsed.pathname.replace(/\\/g, "/").toLowerCase();
      return path.endsWith("/history.html") || path.endsWith("history.html");
    } catch (_) {
      return false;
    }
  }

  function localDownloadsUrl() {
    return new URL("downloads.html", window.location.href).href;
  }

  function isLocalDownloadsUrl(url) {
    try {
      const parsed = new URL(url);
      const path = parsed.pathname.replace(/\\/g, "/").toLowerCase();
      return (
        path.endsWith("/downloads.html") || path.endsWith("downloads.html")
      );
    } catch (_) {
      return false;
    }
  }

  function localBookmarksUrl() {
    return new URL("bookmarks.html", window.location.href).href;
  }

  function isLocalBookmarksUrl(url) {
    try {
      const parsed = new URL(url);
      const path = parsed.pathname.replace(/\\/g, "/").toLowerCase();
      return (
        path.endsWith("/bookmarks.html") || path.endsWith("bookmarks.html")
      );
    } catch (_) {
      return false;
    }
  }

  function localInfoUrl() {
    return new URL("info.html", window.location.href).href;
  }

  function isLocalInfoUrl(url) {
    try {
      const parsed = new URL(url);
      const path = parsed.pathname.replace(/\\/g, "/").toLowerCase();
      return path.endsWith("/info.html") || path.endsWith("info.html");
    } catch (_) {
      return false;
    }
  }

  function toNavigationUrl(raw) {
    const value = String(raw || "").trim();
    if (!value) {
      return null;
    }
    if (looksLikeUrl(value)) {
      if (/^[a-z][a-z0-9+.-]*:\/\//i.test(value)) {
        return value;
      }
      return `https://${value}`;
    }
    const engine = getSearchEngine();
    if (engine.local) {
      return localSearchUrl(value);
    }
    return `${engine.queryUrl}${encodeURIComponent(value)}`;
  }

  function addressBarLabel(url) {
    if (!url || isBlankUrl(url)) {
      return "";
    }
    if (isLocalHistoryUrl(url)) {
      return "History";
    }
    if (isLocalDownloadsUrl(url)) {
      return "Downloads";
    }
    if (isLocalBookmarksUrl(url)) {
      return "Bookmarks";
    }
    if (isLocalInfoUrl(url)) {
      return "Info";
    }
    if (isLocalSearchUrl(url)) {
      try {
        const q = String(new URL(url).searchParams.get("q") || "").trim();
        return q || "Search";
      } catch (_) {
        return "Search";
      }
    }
    return url;
  }

  function titleFromUrl(url) {
    if (!url || isBlankUrl(url)) {
      return "New Tab";
    }
    try {
      const parsed = new URL(url);
      if (isLocalSearchUrl(url)) {
        const q = String(parsed.searchParams.get("q") || "").trim();
        return q || "Search";
      }
      if (isLocalHistoryUrl(url)) {
        return "History";
      }
      if (isLocalDownloadsUrl(url)) {
        return "Downloads";
      }
      if (isLocalBookmarksUrl(url)) {
        return "Bookmarks";
      }
      if (isLocalInfoUrl(url)) {
        return "Info";
      }
      return parsed.hostname.replace(/^www\./, "") || "New Tab";
    } catch (_) {
      return "New Tab";
    }
  }

  /** Hostname / label for tab hover cards. Empty for blank New Tab pages. */
  function domainFromUrl(url) {
    if (!url || isBlankUrl(url)) {
      return "";
    }
    try {
      const parsed = new URL(url);
      if (isLocalSearchUrl(url)) {
        return "QuBrain Search";
      }
      if (isLocalHistoryUrl(url)) {
        return "History";
      }
      if (isLocalDownloadsUrl(url)) {
        return "Downloads";
      }
      if (isLocalBookmarksUrl(url)) {
        return "Bookmarks";
      }
      if (isLocalInfoUrl(url)) {
        return "Info";
      }
      if (parsed.protocol === "file:") {
        return "Local file";
      }
      return parsed.hostname.replace(/^www\./, "") || "";
    } catch (_) {
      return "";
    }
  }

  function isBlankUrl(url) {
    const value = String(url || "")
      .trim()
      .toLowerCase();
    return !value || value === "about:blank" || value.startsWith("about:blank?");
  }

  /** Map CEF/page titles like "about:blank" to the start-page label. */
  function normalizeTabTitle(title, url) {
    if (isBlankUrl(url)) {
      return "New Tab";
    }
    const value = String(title || "").trim();
    if (!value || isBlankUrl(value) || /^about:blank$/i.test(value)) {
      return "New Tab";
    }
    return value;
  }

  /** Tab labels: max 29 chars; vanish with ellipsis from char 27. */
  function formatTabTitle(title) {
    const s = String(title || "").trim() || "New Tab";
    if (s.length <= 27) {
      return s;
    }
    return `${s.slice(0, 27)}…`;
  }

  function urlsMatch(a, b) {
    if (!a || !b) {
      return false;
    }
    if (a === b) {
      return true;
    }
    try {
      const ua = new URL(a);
      const ub = new URL(b);
      ua.hash = "";
      ub.hash = "";
      return ua.href === ub.href;
    } catch (_) {
      return false;
    }
  }

  function measureChromeHeight() {
    const titlebar = document.querySelector(".titlebar");
    const topbar = document.getElementById("topbar");
    const th = titlebar ? titlebar.getBoundingClientRect().height : 36;
    const oh = topbar ? topbar.getBoundingClientRect().height : 44;
    return Math.max(80, Math.round(th + oh));
  }

  window.OmniBrowserUrl = {
    listSearchEngines,
    getSearchEngine,
    setSearchEngine,
    looksLikeUrl,
    toNavigationUrl,
    localSearchUrl,
    isLocalSearchUrl,
    localHistoryUrl,
    isLocalHistoryUrl,
    localDownloadsUrl,
    isLocalDownloadsUrl,
    localBookmarksUrl,
    isLocalBookmarksUrl,
    localInfoUrl,
    isLocalInfoUrl,
    addressBarLabel,
    titleFromUrl,
    domainFromUrl,
    normalizeTabTitle,
    isBlankUrl,
    formatTabTitle,
    urlsMatch,
    measureChromeHeight,
  };
})();
