(() => {
  const DEFAULT_ATTRS = {
    class: "icon",
    "stroke-width": 1.75,
  };

  function available() {
    return Boolean(window.lucide && typeof window.lucide.createIcons === "function");
  }

  function refresh() {
    if (!available()) {
      console.warn("Lucide is not loaded");
      return;
    }
    window.lucide.createIcons({
      icons: window.lucide.icons,
      attrs: DEFAULT_ATTRS,
    });
  }

  // Replace (or insert) a Lucide icon inside |host|.
  function mount(host, name, { id, className } = {}) {
    if (!host) {
      return null;
    }
    const el = document.createElement("i");
    el.setAttribute("data-lucide", name);
    if (id) {
      el.id = id;
    }
    if (className) {
      el.className = className;
    }
    host.replaceChildren(el);
    refresh();
    return host.querySelector("svg");
  }

  window.OmniIcons = {
    available,
    refresh,
    mount,
  };
})();
