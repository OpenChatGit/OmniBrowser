(() => {
  const navs = [...document.querySelectorAll("[data-settings-nav]")];
  const panels = [...document.querySelectorAll("[data-settings-panel]")];
  const appearanceInputs = [...document.querySelectorAll('input[name="appearance"]')];
  const searchSelect = document.getElementById("settings-search-engine");
  const adblockEnabled = document.getElementById("settings-adblock-enabled");
  const adblockAggressive = document.getElementById("settings-adblock-aggressive");
  const status = document.getElementById("settings-status");
  const bridge = window.OmniBridge;
  let adblockAvailable = Boolean(bridge && typeof bridge.adblockSet === "function");

  function showSection(id) {
    navs.forEach((nav) => {
      const selected = nav.dataset.settingsNav === id;
      nav.classList.toggle("is-active", selected);
      if (selected) nav.setAttribute("aria-current", "page");
      else nav.removeAttribute("aria-current");
    });
    panels.forEach((panel) => {
      panel.hidden = panel.dataset.settingsPanel !== id;
    });
  }

  function showError(error) {
    status.textContent = `Could not save setting: ${error.message || error}`;
  }

  function clearError() {
    status.textContent = "";
  }

  function setAppearance(value) {
    const valid = ["system", "dark", "light"].includes(value) ? value : "system";
    appearanceInputs.forEach((input) => {
      input.checked = input.value === valid;
    });
  }

  function setSearchEngine(value) {
    const valid = [...searchSelect.options].some((option) => option.value === value);
    searchSelect.value = valid ? value : "google";
  }

  function setAdblock(state) {
    if (!state || typeof state !== "object") return;
    adblockEnabled.checked = Boolean(state.enabled);
    adblockAggressive.checked = Boolean(state.aggressive);
    adblockAggressive.disabled = !adblockEnabled.checked;
  }

  async function refresh() {
    setAppearance(window.OmniTheme ? OmniTheme.preference() : "system");
    try {
      setSearchEngine(localStorage.getItem("omni.searchEngine") || "google");
    } catch (_) {
      setSearchEngine("google");
    }
    if (!bridge || typeof bridge.settingsGet !== "function") {
      adblockAvailable = false;
      adblockEnabled.disabled = true;
      adblockAggressive.disabled = true;
      return;
    }
    const results = await Promise.allSettled([
      bridge.settingsGet("appearance"),
      bridge.settingsGet("omni.searchEngine"),
      bridge.adblockGet(),
    ]);
    if (results[0].status === "fulfilled") {
      setAppearance(results[0].value.value);
    }
    if (results[1].status === "fulfilled" &&
        typeof results[1].value.value === "string") {
      setSearchEngine(results[1].value.value);
    }
    if (results[2].status === "fulfilled") {
      adblockAvailable = true;
      adblockEnabled.disabled = false;
      setAdblock(results[2].value);
    } else {
      adblockAvailable = false;
      adblockEnabled.disabled = true;
      adblockAggressive.disabled = true;
    }
  }

  navs.forEach((nav) => {
    nav.addEventListener("click", () => showSection(nav.dataset.settingsNav));
  });

  appearanceInputs.forEach((input) => {
    input.addEventListener("change", async () => {
      if (!input.checked) return;
      clearError();
      const previous = window.OmniTheme ? OmniTheme.preference() : "system";
      try {
        if (bridge && typeof bridge.settingsSet === "function") {
          const result = await bridge.settingsSet("appearance", input.value);
          if (!result || result.ok !== true) throw new Error("Save failed");
          OmniTheme.applyExternal(input.value);
        } else if (window.OmniTheme) {
          OmniTheme.setPreference(input.value);
        }
      } catch (error) {
        setAppearance(previous);
        showError(error);
      }
    });
  });

  searchSelect.addEventListener("change", async () => {
    clearError();
    let previous = "google";
    try { previous = localStorage.getItem("omni.searchEngine") || "google"; } catch (_) {}
    const value = searchSelect.value;
    try {
      if (bridge && typeof bridge.settingsSet === "function") {
        const result = await bridge.settingsSet("omni.searchEngine", value);
        if (!result || result.ok !== true) throw new Error("Save failed");
      }
      try { localStorage.setItem("omni.searchEngine", value); } catch (_) {}
    } catch (error) {
      setSearchEngine(previous);
      showError(error);
    }
  });

  async function saveAdblock() {
    clearError();
    const enabled = adblockEnabled.checked;
    const aggressive = adblockAggressive.checked;
    adblockEnabled.disabled = true;
    adblockAggressive.disabled = true;
    try {
      if (!bridge || typeof bridge.adblockSet !== "function") {
        throw new Error("Browser API unavailable");
      }
      const state = await bridge.adblockSet({ enabled, aggressive });
      setAdblock(state);
    } catch (error) {
      showError(error);
      await refresh();
    } finally {
      adblockEnabled.disabled = !adblockAvailable;
      adblockAggressive.disabled = !adblockAvailable || !adblockEnabled.checked;
    }
  }

  adblockEnabled.addEventListener("change", saveAdblock);
  adblockAggressive.addEventListener("change", saveAdblock);
  window.addEventListener("focus", refresh);
  document.addEventListener("visibilitychange", () => {
    if (!document.hidden) refresh();
  });
  window.addEventListener("omni-theme-change", () => {
    if (window.OmniTheme) setAppearance(OmniTheme.preference());
  });
  refresh();
})();
