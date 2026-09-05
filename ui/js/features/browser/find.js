(() => {
  let isFindOpen = false;
  let lastQuery = "";

  function openFind() {
    if (!window.OmniBridge || typeof window.OmniBridge.browserShowFind !== "function") return;
    OmniBridge.browserShowFind().catch(() => {});
    isFindOpen = true;
  }

  function closeFind() {
    isFindOpen = false;
    if (window.OmniBridge && typeof window.OmniBridge.browserHideFind === "function") {
      window.OmniBridge.browserHideFind().catch(() => {});
    }
  }

  function toggleFind() {
    if (window.OmniBridge && typeof window.OmniBridge.browserToggleFind === "function") {
      OmniBridge.browserToggleFind().catch(() => {});
    }
    isFindOpen = !isFindOpen;
  }

  function isOpen() {
    return isFindOpen;
  }

  function setIsOpen(open) {
    isFindOpen = Boolean(open);
  }

  function setLastQuery(q) {
    lastQuery = q || "";
  }

  // Handle global Escape if focused in shell
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && isFindOpen) {
      closeFind();
    }
  });

  window.OmniFind = {
    open: openFind,
    close: closeFind,
    toggle: toggleFind,
    isOpen,
    setIsOpen,
    setLastQuery,
  };
})();
