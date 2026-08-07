(() => {
  function bootTitlebar() {
    OmniWindowIcons.fillTitlebarControls();

    const btnMin = document.getElementById("titlebar-min");
    const btnMax = document.getElementById("titlebar-max");
    const btnClose = document.getElementById("titlebar-close");

    function icons() {
      return {
        maximize: document.getElementById("icon-maximize"),
        restore: document.getElementById("icon-restore"),
      };
    }

    function setMaximized(maximized) {
      document.body.classList.toggle("is-maximized", Boolean(maximized));
      const { maximize, restore } = icons();
      if (maximize && restore) {
        maximize.classList.toggle("hidden", Boolean(maximized));
        restore.classList.toggle("hidden", !maximized);
      }
      if (btnMax) {
        btnMax.setAttribute("aria-label", maximized ? "Restore" : "Maximize");
      }
    }

    async function syncMaximized() {
      try {
        const state = await OmniBridge.windowIsMaximized();
        setMaximized(state && state.maximized);
      } catch (_) {
        /* ignore */
      }
    }

    btnMin.addEventListener("click", () => {
      OmniBridge.windowMinimize().catch(console.error);
    });
    btnMax.addEventListener("click", async () => {
      try {
        const result = await OmniBridge.windowToggleMaximize();
        setMaximized(result && result.maximized);
      } catch (err) {
        console.error(err);
      }
    });
    btnClose.addEventListener("click", () => {
      OmniBridge.windowClose().catch(console.error);
    });

    document.querySelector(".titlebar").addEventListener("dblclick", async (event) => {
      if (event.target.closest(".titlebar-controls, .titlebar-menu")) {
        return;
      }
      try {
        const result = await OmniBridge.windowToggleMaximize();
        setMaximized(result && result.maximized);
      } catch (err) {
        console.error(err);
      }
    });

    syncMaximized();
    window.addEventListener("resize", syncMaximized);
  }

  window.OmniTitlebar = { bootTitlebar };
})();
