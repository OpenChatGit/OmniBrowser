(() => {
  const STORAGE_KEY = "omni.appearance";
  const THEME_KEY = "omni.appearance.theme";
  const BRAND_DARK = "assets/QuBrain-new/q-white.svg";
  const BRAND_LIGHT = "assets/QuBrain-new/q-black.svg";

  let preference = "system";
  let resolved = "dark";
  let lastNativeTheme = null;
  let media = null;

  function systemIsDark() {
    return !window.matchMedia ||
      window.matchMedia("(prefers-color-scheme: dark)").matches;
  }

  function normalizePref(value) {
    if (value === "light" || value === "dark" || value === "system") {
      return value;
    }
    return "system";
  }

  function rememberNativeTheme(nativeTheme) {
    if (nativeTheme === "light" || nativeTheme === "dark") {
      lastNativeTheme = nativeTheme;
    }
  }

  function computeResolved(pref, nativeTheme) {
    if (document.documentElement.getAttribute("data-private") === "1") {
      return "dark";
    }
    if (pref === "light") {
      return "light";
    }
    if (pref === "dark") {
      return "dark";
    }
    rememberNativeTheme(nativeTheme);
    if (lastNativeTheme === "light" || lastNativeTheme === "dark") {
      return lastNativeTheme;
    }
    return systemIsDark() ? "dark" : "light";
  }

  function brandMark() {
    return resolved === "light" ? BRAND_LIGHT : BRAND_DARK;
  }

  function applyBrandMarks() {
    const src = brandMark();
    document.querySelectorAll(
      ".browser-brand-mark, .serp-brand-mark, .hist-brand-mark, .info-hero-mark"
    ).forEach((img) => {
      if (img && img.getAttribute("src") !== src) {
        img.setAttribute("src", src);
      }
    });
    const searchMark = document.getElementById("browser-search-mark");
    if (searchMark && searchMark.dataset.brandLock !== "engine") {
      const current = searchMark.getAttribute("src") || "";
      if (
        current.indexOf("qubrain") !== -1 ||
        current.indexOf("QuBrain-new") !== -1
      ) {
        searchMark.setAttribute("src", src);
      }
    }
  }

  function persist(nextPref, nextTheme) {
    try {
      localStorage.setItem(STORAGE_KEY, nextPref);
      localStorage.setItem(THEME_KEY, nextTheme);
    } catch (_) {
      // ignore
    }
  }

  function apply(nextPref, persistLocal, nativeTheme) {
    preference = normalizePref(nextPref);
    rememberNativeTheme(nativeTheme);
    resolved = computeResolved(preference, nativeTheme);
    document.documentElement.setAttribute("data-theme", resolved);
    document.documentElement.style.colorScheme = resolved;
    if (persistLocal) {
      persist(preference, resolved);
    }
    applyBrandMarks();
    window.dispatchEvent(
      new CustomEvent("omni-theme-change", {
        detail: { preference, theme: resolved },
      })
    );
  }

  function setPreference(nextPref) {
    apply(nextPref, true);
    if (window.OmniBridge && typeof OmniBridge.settingsSet === "function") {
      OmniBridge.settingsSet("appearance", preference).catch(() => {});
    }
  }

  function applyExternal(nextPref, nativeTheme) {
    apply(nextPref, true, nativeTheme);
  }

  function bootFromCache() {
    let cached = "system";
    let cachedTheme = "";
    try {
      cached = localStorage.getItem(STORAGE_KEY) || "system";
      cachedTheme = localStorage.getItem(THEME_KEY) || "";
    } catch (_) {
      cached = "system";
    }
    apply(cached, false, cachedTheme);
  }

  function syncFromNative() {
    if (!window.OmniBridge || typeof OmniBridge.settingsGet !== "function") {
      return;
    }
    OmniBridge.settingsGet("appearance")
      .then((res) => {
        const value =
          res && res.value != null
            ? res.value
            : res && res.settings && res.settings.appearance;
        if (typeof value === "string") {
          apply(value, true, res && res.theme);
        }
      })
      .catch(() => {});
  }

  function boot() {
    bootFromCache();
    if (window.matchMedia) {
      media = window.matchMedia("(prefers-color-scheme: dark)");
      const onChange = () => {
        if (preference !== "system" || lastNativeTheme) {
          return;
        }
        apply("system", false);
      };
      if (typeof media.addEventListener === "function") {
        media.addEventListener("change", onChange);
      } else if (typeof media.addListener === "function") {
        media.addListener(onChange);
      }
    }
    syncFromNative();
    if (
      window.OmniBridge &&
      typeof OmniBridge.browserSubscribe === "function"
    ) {
      OmniBridge.browserSubscribe((msg) => {
        if (!msg || msg.type !== "appearance.changed") {
          return;
        }
        apply(msg.preference || "system", true, msg.theme);
      }).catch(() => {});
    }
  }

  window.OmniTheme = {
    boot,
    setPreference,
    applyExternal,
    brandMark,
    preference: () => preference,
    resolved: () => resolved,
  };
})();
