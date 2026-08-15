(() => {
  document.addEventListener("DOMContentLoaded", () => {
    try { OmniIcons.refresh(); } catch (e) { console.error("OmniIcons error:", e); }
    try { OmniTooltip.bootTooltips(); } catch (e) { console.error("OmniTooltip error:", e); }
    try { OmniTitlebar.bootTitlebar(); } catch (e) { console.error("OmniTitlebar error:", e); }
    try { OmniBrowserBoot.bootBrowser(); } catch (e) { console.error("OmniBrowserBoot error:", e); }
    try {
      if (window.OmniMedia) {
        OmniMedia.boot();
      }
    } catch (e) {
      console.error("OmniMedia error:", e);
    }
    try { OmniMenu.bootMenu(); } catch (e) { console.error("OmniMenu error:", e); }
    try {
      if (window.OmniAdblock && typeof OmniAdblock.boot === "function") {
        OmniAdblock.boot();
      }
    } catch (e) {
      console.error("OmniAdblock error:", e);
    }
    try {
      if (window.OmniAutocomplete && typeof OmniAutocomplete.boot === "function") {
        OmniAutocomplete.boot();
      }
    } catch (e) {
      console.error("OmniAutocomplete error:", e);
    }
  });
})();
