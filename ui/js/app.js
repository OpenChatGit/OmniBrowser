(() => {
  document.addEventListener("DOMContentLoaded", () => {
    OmniIcons.refresh();
    OmniTooltip.bootTooltips();
    OmniTitlebar.bootTitlebar();
    OmniBrowserBoot.bootBrowser();
    if (window.OmniMedia) {
      OmniMedia.boot();
    }
    OmniMenu.bootMenu();
  });
})();
