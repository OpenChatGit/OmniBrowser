(() => {
  // Native Windows chrome glyphs (Segoe Fluent Icons / Segoe MDL2 Assets).
  const GLYPHS = {
    minimize: "\uE921", // ChromeMinimize
    maximize: "\uE922", // ChromeMaximize
    restore: "\uE923", // ChromeRestore
    close: "\uE8BB", // ChromeClose
  };

  function mount(host, name, { id, className } = {}) {
    if (!host || !GLYPHS[name]) {
      return null;
    }
    const el = document.createElement("span");
    el.className = "window-icon";
    el.setAttribute("aria-hidden", "true");
    el.textContent = GLYPHS[name];
    if (id) {
      el.id = id;
    }
    if (className) {
      el.classList.add(...String(className).split(/\s+/).filter(Boolean));
    }
    host.appendChild(el);
    return el;
  }

  function fillTitlebarControls() {
    const min = document.getElementById("titlebar-min");
    const max = document.getElementById("titlebar-max");
    const close = document.getElementById("titlebar-close");
    if (min) {
      min.replaceChildren();
      mount(min, "minimize");
    }
    if (max) {
      max.replaceChildren();
      mount(max, "maximize", { id: "icon-maximize" });
      mount(max, "restore", { id: "icon-restore", className: "hidden" });
    }
    if (close) {
      close.replaceChildren();
      mount(close, "close");
    }
  }

  window.OmniWindowIcons = {
    mount,
    fillTitlebarControls,
  };
})();
