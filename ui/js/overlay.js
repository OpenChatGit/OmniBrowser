(() => {
  // Overlay: tab hover tip, History flyout, Global Media Controls.
  const root = document.getElementById("overlay-root");
  const FALLBACK_FAVICON = "assets/qubrain.svg";

  let panel = null;
  let layout = null;
  let resizeObserver = null;
  let lastReportW = 0;
  let lastReportH = 0;

  function reportSize() {
    if (!layout || !window.OmniBridge) {
      return;
    }
    const rect = layout.getBoundingClientRect();
    const width = Math.max(1, Math.ceil(rect.width));
    const height = Math.max(40, Math.ceil(rect.height));
    if (width === lastReportW && height === lastReportH) {
      return;
    }
    lastReportW = width;
    lastReportH = height;
    if (height > 0) {
      OmniBridge.overlayResize({ width, height }).catch(() => {});
    }
  }

  function observeSize() {
    if (resizeObserver) {
      resizeObserver.disconnect();
      resizeObserver = null;
    }
    if (!layout || typeof ResizeObserver !== "function") {
      return;
    }
    resizeObserver = new ResizeObserver(() => reportSize());
    resizeObserver.observe(layout);
  }

  function el(tag, className, text) {
    const node = document.createElement(tag);
    if (className) {
      node.className = className;
    }
    if (text != null) {
      node.textContent = text;
    }
    return node;
  }

  const ICON_VOLUME =
    '<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"/><path d="M15.54 8.46a5 5 0 0 1 0 7.07"/><path d="M19.07 4.93a10 10 0 0 1 0 14.14"/></svg>';
  const ICON_VOLUME_OFF =
    '<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"/><line x1="22" x2="16" y1="9" y2="15"/><line x1="16" x2="22" y1="9" y2="15"/></svg>';
  const ICON_GAUGE =
    '<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="m12 14 4-4"/><path d="M3.34 19a10 10 0 1 1 17.32 0"/></svg>';
  const ICON_PLAY =
    '<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.1" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><polygon points="6 3 20 12 6 21 6 3"/></svg>';
  const ICON_PAUSE =
    '<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.1" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="14" y="4" width="4" height="16" rx="1"/><rect x="6" y="4" width="4" height="16" rx="1"/></svg>';
  const ICON_PIP =
    '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.85" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M21 9V6a2 2 0 0 0-2-2H4a2 2 0 0 0-2 2v10c0 1.1.9 2 2 2h4"/><rect width="10" height="7" x="12" y="13" rx="2"/></svg>';
  const ICON_SKIP_BACK =
    '<svg xmlns="http://www.w3.org/2000/svg" width="17" height="17" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.85" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><polygon points="19 20 9 12 19 4 19 20"/><line x1="5" x2="5" y1="19" y2="5"/></svg>';
  const ICON_SKIP_FWD =
    '<svg xmlns="http://www.w3.org/2000/svg" width="17" height="17" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.85" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><polygon points="5 4 15 12 5 20 5 4"/><line x1="19" x2="19" y1="5" y2="19"/></svg>';
  const ICON_BACK_10 =
    '<svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.85" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M3 12a9 9 0 1 0 3-6.7"/><path d="M3 4v5h5"/><text x="12" y="15.5" text-anchor="middle" fill="currentColor" stroke="none" font-size="7.5" font-family="Segoe UI, sans-serif" font-weight="650">10</text></svg>';
  const ICON_FWD_10 =
    '<svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.85" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M21 12a9 9 0 1 1-3-6.7"/><path d="M21 4v5h-5"/><text x="12" y="15.5" text-anchor="middle" fill="currentColor" stroke="none" font-size="7.5" font-family="Segoe UI, sans-serif" font-weight="650">10</text></svg>';
  const ICON_MUSIC =
    '<svg xmlns="http://www.w3.org/2000/svg" width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.85" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/></svg>';

  let mediaScrubbing = false;
  let mediaTabId = "";

  function tipRow(iconHtml, text) {
    const row = el("div", "omni-tab-tip-row");
    row.innerHTML = iconHtml;
    row.append(el("span", "", text));
    return row;
  }

  function memoryLabel(data) {
    if (typeof data.memoryMb === "number" && Number.isFinite(data.memoryMb)) {
      return `Memory usage: ${Math.max(0, Math.round(data.memoryMb))} MB`;
    }
    return "Memory usage: …";
  }

  function fillTabTip(panelNode, data) {
    panelNode.replaceChildren();
    const head = el("div", "omni-tab-tip-head");
    head.append(el("div", "omni-tab-tip-title", data.title || "New Tab"));
    if (data.domain) {
      head.append(el("div", "omni-tab-tip-domain", data.domain));
    }
    panelNode.append(head);

    const meta = el("div", "omni-tab-tip-meta");
    if (data.audioPlaying) {
      meta.append(
        tipRow(
          data.audioMuted ? ICON_VOLUME_OFF : ICON_VOLUME,
          data.audioMuted ? "This tab is muted" : "This tab is playing audio"
        )
      );
    }
    meta.append(tipRow(ICON_GAUGE, memoryLabel(data)));
    panelNode.append(meta);
  }

  function patchTabTip(data) {
    if (!panel || !panel.classList.contains("omni-tab-tip")) {
      return false;
    }
    const titleEl = panel.querySelector(".omni-tab-tip-title");
    if (titleEl) {
      titleEl.textContent = data.title || "New Tab";
    }
    const head = panel.querySelector(".omni-tab-tip-head");
    let domainEl = panel.querySelector(".omni-tab-tip-domain");
    if (data.domain) {
      if (!domainEl) {
        domainEl = el("div", "omni-tab-tip-domain", data.domain);
        head?.append(domainEl);
      } else {
        domainEl.textContent = data.domain;
      }
    } else if (domainEl) {
      domainEl.remove();
    }

    const meta = panel.querySelector(".omni-tab-tip-meta");
    if (!meta) {
      return false;
    }
    const rows = meta.querySelectorAll(".omni-tab-tip-row");
    const audioWanted = Boolean(data.audioPlaying);
    const hasAudioRow = rows.length > 1;
    if (audioWanted !== hasAudioRow) {
      fillTabTip(panel, data);
      requestAnimationFrame(reportSize);
      return true;
    }
    if (audioWanted && rows[0]) {
      rows[0].innerHTML = data.audioMuted ? ICON_VOLUME_OFF : ICON_VOLUME;
      rows[0].append(
        el(
          "span",
          "",
          data.audioMuted ? "This tab is muted" : "This tab is playing audio"
        )
      );
    }
    const memRow = rows[rows.length - 1];
    const memLabel = memRow && memRow.querySelector("span");
    if (memLabel) {
      memLabel.textContent = memoryLabel(data);
    }
    return true;
  }

  function renderTabTip(payload) {
    document.body.classList.remove("is-history", "is-media", "is-shields");
    document.body.classList.add("is-tab-tip");
    const data = payload || {};

    if (patchTabTip(data)) {
      return;
    }

    lastReportW = 0;
    lastReportH = 0;
    panel = el("div", "omni-tab-tip");
    layout = panel;
    panel.setAttribute("role", "tooltip");
    fillTabTip(panel, data);

    root.replaceChildren(panel);
    observeSize();
    requestAnimationFrame(reportSize);
  }

  function bindFavicon(img, sources) {
    const list =
      Array.isArray(sources) && sources.length > 0
        ? sources.slice()
        : [FALLBACK_FAVICON];
    let index = 0;
    img.decoding = "async";
    img.referrerPolicy = "no-referrer";
    const tryNext = () => {
      if (index >= list.length) {
        img.removeEventListener("error", tryNext);
        img.src = FALLBACK_FAVICON;
        return;
      }
      img.src = list[index];
      index += 1;
    };
    img.addEventListener("error", tryNext);
    tryNext();
  }

  function sendCommand(command) {
    if (!window.OmniBridge || typeof OmniBridge.overlayCommand !== "function") {
      return;
    }
    OmniBridge.overlayCommand(command).catch(() => {});
  }

  function mediaControl(action, value) {
    if (
      !window.OmniBridge ||
      typeof OmniBridge.browserMediaControl !== "function"
    ) {
      return;
    }
    const params = { tabId: mediaTabId, action };
    if (value != null && Number.isFinite(Number(value))) {
      params.value = Number(value);
    }
    OmniBridge.browserMediaControl(action, params).catch(() => {});
  }

  function faviconForMedia(origin, pageUrl) {
    const host = String(origin || "").trim();
    if (host) {
      return `https://www.google.com/s2/favicons?domain=${encodeURIComponent(host)}&sz=32`;
    }
    try {
      const u = new URL(pageUrl);
      return `https://www.google.com/s2/favicons?domain=${encodeURIComponent(u.hostname)}&sz=32`;
    } catch (_) {
      return FALLBACK_FAVICON;
    }
  }

  function fillMedia(panelNode, data) {
    panelNode.replaceChildren();
    mediaTabId = String(data.tabId || "");

    const main = el("div", "omni-media-main");
    const artUrl = String(data.artwork || "").trim();
    if (artUrl) {
      const art = el("img", "omni-media-art");
      art.alt = "";
      art.referrerPolicy = "no-referrer";
      art.src = artUrl;
      main.append(art);
    } else {
      const fallback = el("div", "omni-media-art-fallback");
      fallback.innerHTML = ICON_MUSIC;
      main.append(fallback);
    }

    const meta = el("div", "omni-media-meta");
    const source = el("div", "omni-media-source");
    const fav = el("img", "omni-media-favicon");
    fav.alt = "";
    fav.referrerPolicy = "no-referrer";
    fav.src = faviconForMedia(data.origin, data.pageUrl);
    source.append(fav, el("span", "omni-media-origin", data.origin || "Media"));
    meta.append(source);
    meta.append(el("p", "omni-media-title", data.title || "Playing media"));
    meta.append(el("p", "omni-media-artist", data.artist || ""));
    main.append(meta);

    const side = el("div", "omni-media-side");
    const pip = el("button", "omni-media-icon-btn");
    pip.type = "button";
    pip.title = "Picture in picture";
    pip.setAttribute("aria-label", "Picture in picture");
    pip.innerHTML = ICON_PIP;
    pip.hidden = !data.canPip;
    pip.addEventListener("click", (event) => {
      event.preventDefault();
      mediaControl("pip");
    });
    const play = el("button", "omni-media-play");
    play.type = "button";
    const playing = Boolean(data.playing);
    play.setAttribute("aria-label", playing ? "Pause" : "Play");
    play.innerHTML = playing ? ICON_PAUSE : ICON_PLAY;
    play.addEventListener("click", (event) => {
      event.preventDefault();
      mediaControl("toggle");
    });
    side.append(pip, play);
    main.append(side);
    panelNode.append(main);

    const seek = el("div", "omni-media-seek");
    function seekBtn(html, action, value, label) {
      const b = el("button", "omni-media-icon-btn");
      b.type = "button";
      b.title = label;
      b.setAttribute("aria-label", label);
      b.innerHTML = html;
      b.addEventListener("click", (event) => {
        event.preventDefault();
        mediaControl(action, value);
      });
      return b;
    }
    seek.append(seekBtn(ICON_SKIP_BACK, "seekStart", undefined, "Seek to start"));
    seek.append(seekBtn(ICON_BACK_10, "seekRelative", -10, "Back 10 seconds"));

    const range = el("input", "omni-media-range");
    range.type = "range";
    range.min = "0";
    range.step = "0.1";
    const duration = Math.max(0, Number(data.duration) || 0);
    const current = Math.max(0, Number(data.currentTime) || 0);
    range.max = duration > 0 ? String(duration) : "1";
    range.value = duration > 0 ? String(Math.min(duration, current)) : "0";
    range.disabled = duration <= 0;
    range.setAttribute("aria-label", "Seek");
    range.addEventListener("pointerdown", () => {
      mediaScrubbing = true;
    });
    range.addEventListener("pointerup", () => {
      mediaScrubbing = false;
    });
    range.addEventListener("change", () => {
      mediaScrubbing = false;
      mediaControl("seek", Number(range.value) || 0);
    });
    range.addEventListener("input", () => {
      mediaScrubbing = true;
    });
    seek.append(range);
    seek.append(seekBtn(ICON_FWD_10, "seekRelative", 10, "Forward 10 seconds"));
    seek.append(seekBtn(ICON_SKIP_FWD, "seekEnd", undefined, "Seek to end"));
    panelNode.append(seek);
  }

  function patchMedia(data) {
    if (!panel || !panel.classList.contains("omni-media")) {
      return false;
    }
    mediaTabId = String(data.tabId || mediaTabId);
    const titleEl = panel.querySelector(".omni-media-title");
    const artistEl = panel.querySelector(".omni-media-artist");
    const originEl = panel.querySelector(".omni-media-origin");
    const play = panel.querySelector(".omni-media-play");
    const pip = panel.querySelector('.omni-media-icon-btn[aria-label="Picture in picture"]');
    const range = panel.querySelector(".omni-media-range");
    if (titleEl) {
      titleEl.textContent = data.title || "Playing media";
    }
    if (artistEl) {
      artistEl.textContent = data.artist || "";
    }
    if (originEl) {
      originEl.textContent = data.origin || "Media";
    }
    if (play) {
      const playing = Boolean(data.playing);
      play.setAttribute("aria-label", playing ? "Pause" : "Play");
      play.innerHTML = playing ? ICON_PAUSE : ICON_PLAY;
    }
    if (pip) {
      pip.hidden = !data.canPip;
    }
    if (range && !mediaScrubbing) {
      const duration = Math.max(0, Number(data.duration) || 0);
      const current = Math.max(0, Number(data.currentTime) || 0);
      range.max = duration > 0 ? String(duration) : "1";
      range.value = duration > 0 ? String(Math.min(duration, current)) : "0";
      range.disabled = duration <= 0;
    }
    const art = panel.querySelector(".omni-media-art, .omni-media-art-fallback");
    const artUrl = String(data.artwork || "").trim();
    if (art && artUrl && art.tagName === "IMG" && art.src !== artUrl) {
      art.src = artUrl;
    }
    return true;
  }

  function renderMedia(payload) {
    document.body.classList.remove("is-tab-tip", "is-history", "is-shields");
    document.body.classList.add("is-media");
    const data = payload || {};
    if (patchMedia(data)) {
      requestAnimationFrame(reportSize);
      return;
    }
    lastReportW = 0;
    lastReportH = 0;
    mediaScrubbing = false;
    panel = el("div", "omni-media");
    layout = panel;
    panel.setAttribute("role", "dialog");
    panel.setAttribute("aria-label", "Media controls");
    fillMedia(panel, data);
    root.replaceChildren(panel);
    observeSize();
    requestAnimationFrame(reportSize);
  }

  function renderUpdate(payload) {
    document.body.classList.remove(
      "is-tab-tip", "is-history", "is-media", "is-shields", "is-find"
    );
    document.body.classList.add("is-update");
    const data = payload || {};
    lastReportW = 0;
    lastReportH = 0;
    panel = el("div", "update-flyout");
    layout = panel;
    panel.setAttribute("role", "dialog");
    panel.setAttribute("aria-labelledby", "update-flyout-title");

    const header = el("div", "update-flyout-header");
    const badge = el("div", "update-flyout-badge-wrap");
    const icon = el("img", "update-flyout-logo");
    icon.src = "assets/QuBrain-new/q-white.svg";
    icon.alt = "OmniBrowser";
    const headerText = el("div", "update-flyout-header-text");
    headerText.append(el("span", "update-flyout-title", "Update Available"));
    headerText.lastChild.id = "update-flyout-title";
    headerText.append(el("span", "update-flyout-version", data.version || "New version"));
    badge.append(icon, headerText);
    const close = el("button", "update-flyout-close");
    close.type = "button";
    close.setAttribute("aria-label", "Close update panel");
    close.innerHTML = '<i data-lucide="x" class="icon"></i>';
    close.addEventListener("click", () => window.OmniBridge?.overlayHide?.().catch(() => {}));
    header.append(badge, close);
    panel.append(header);

    const content = el("div", "update-flyout-content");
    content.append(el("p", "update-flyout-notes", data.notes || "A new version of OmniBrowser is available."));
    panel.append(content);
    const actions = el("div", "update-flyout-actions");
    const link = el("button", "update-btn-release");
    link.type = "button";
    link.innerHTML = '<i data-lucide="download" class="icon"></i><span>Download Update</span>';
    link.addEventListener("click", async () => {
      if (!data.downloadUrl || !window.OmniBridge?.call) {
        window.open(data.releaseUrl || "https://github.com/OpenChatGit/OmniBrowser/releases", "_blank", "noopener");
        return;
      }
      link.disabled = true;
      link.querySelector("span").textContent = "Preparing update…";
      try {
        await window.OmniBridge.call("app.installUpdate", {
          downloadUrl: data.downloadUrl,
          sha256: data.sha256,
        });
        link.querySelector("span").textContent = "Installing and restarting…";
      } catch (error) {
        link.disabled = false;
        link.querySelector("span").textContent = "Retry update";
        const message = el("p", "update-flyout-notes", error?.message || "Could not start the update.");
        content.append(message);
      }
    });
    const later = el("button", "update-btn-dismiss", "Later");
    later.type = "button";
    later.addEventListener("click", () => window.OmniBridge?.overlayHide?.().catch(() => {}));
    actions.append(link, later);
    panel.append(actions);
    root.replaceChildren(panel);
    observeSize();
    requestAnimationFrame(reportSize);
  }

  function historyCommandForItem(item) {
    if (!item || typeof item !== "object") {
      return null;
    }
    if (item.type === "action" && item.action) {
      return { action: item.action };
    }
    if (item.type === "closed" && item.closedId) {
      return { action: "restore-tab", closedId: item.closedId };
    }
    if (item.url) {
      return { action: "navigate", url: item.url };
    }
    return null;
  }

  function renderHistory(payload) {
    document.body.classList.remove("is-tab-tip", "is-media", "is-shields");
    document.body.classList.add("is-history");

    lastReportW = 0;
    lastReportH = 0;

    panel = el("div", "omni-history");
    panel.setAttribute("role", "menu");
    layout = panel;

    const items = Array.isArray(payload.items) ? payload.items : [];
    items.forEach((item) => {
      if (!item || typeof item !== "object") {
        return;
      }
      if (item.type === "separator") {
        panel.append(el("div", "omni-history-sep"));
        return;
      }

      const btn = el("button", "omni-history-item");
      btn.type = "button";
      btn.setAttribute("role", "menuitem");

      const icon = el("img", "omni-history-favicon");
      icon.alt = "";
      icon.draggable = false;
      if (item.type === "action") {
        icon.classList.add("is-action");
        icon.hidden = true;
      } else {
        bindFavicon(icon, item.favicons);
      }

      const label = el("span", "omni-history-label", item.title || item.url || "");
      btn.append(icon, label);

      if (item.shortcut) {
        btn.append(el("span", "omni-history-shortcut", item.shortcut));
      }

      const command = historyCommandForItem(item);
      if (command) {
        btn.addEventListener("click", (event) => {
          event.preventDefault();
          event.stopPropagation();
          sendCommand(command);
        });
      } else {
        btn.disabled = true;
      }

      panel.append(btn);
    });

    root.replaceChildren(panel);
    observeSize();
    requestAnimationFrame(reportSize);
  }

  const ICON_GEAR =
    '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.85" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z"/><circle cx="12" cy="12" r="3"/></svg>';
  const ICON_CHEVRON =
    '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.85" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="m6 9 6 6 6-6"/></svg>';

  let shieldsAdvancedOpen = false;

  function formatShieldCount(n) {
    return (Number(n) || 0).toLocaleString();
  }

  async function applyShieldAction(action, data) {
    if (!window.OmniBridge) {
      return data;
    }
    const host = String(data.host || "");
    try {
      if (action === "toggle-site") {
        if (host) {
          return await OmniBridge.adblockAllowlist(host, Boolean(data.siteShieldsUp));
        }
        return await OmniBridge.adblockSet({
          enabled: !data.enabled,
          host: "",
        });
      }
      if (action === "toggle-aggressive") {
        return await OmniBridge.adblockSet({
          aggressive: !data.aggressive,
          host,
        });
      }
      if (action === "toggle-global") {
        return await OmniBridge.adblockSet({
          enabled: !data.enabled,
          host,
        });
      }
    } catch (_) {
      /* ignore */
    }
    return data;
  }

  function normalizeShieldData(raw, previous) {
    const prev = previous || {};
    const next = raw && typeof raw === "object" ? raw : {};
    const host = String(next.host || prev.host || "");
    const enabled =
      typeof next.enabled === "boolean" ? next.enabled : Boolean(prev.enabled);
    let siteUp = true;
    if (typeof next.siteShieldsUp === "boolean") {
      siteUp = next.siteShieldsUp;
    } else if (host && Array.isArray(next.allowlist)) {
      siteUp =
        enabled &&
        !next.allowlist.some((entry) => {
          const e = String(entry || "").replace(/^www\./i, "");
          return e === host || host.endsWith(`.${e}`);
        });
    } else if (typeof prev.siteShieldsUp === "boolean") {
      siteUp = prev.siteShieldsUp;
    }
    return {
      host,
      pageUrl: String(next.pageUrl || prev.pageUrl || ""),
      enabled,
      aggressive:
        typeof next.aggressive === "boolean"
          ? next.aggressive
          : Boolean(prev.aggressive),
      siteShieldsUp: siteUp,
      blockedForHost:
        Number(next.blockedForHost != null ? next.blockedForHost : prev.blockedForHost) ||
        0,
      blockedTotal:
        Number(next.blockedTotal != null ? next.blockedTotal : prev.blockedTotal) ||
        0,
      allowlist: Array.isArray(next.allowlist) ? next.allowlist : prev.allowlist || [],
    };
  }

  function fillShields(panelNode, data) {
    panelNode.replaceChildren();
    const state = normalizeShieldData(data, null);

    const head = el("div", "omni-shield-head");
    const site = el("div", "omni-shield-site");
    const domainRow = el("div", "omni-shield-domain-row");
    if (state.host) {
      const fav = el("img", "omni-shield-favicon");
      fav.alt = "";
      fav.referrerPolicy = "no-referrer";
      fav.src = `https://www.google.com/s2/favicons?domain=${encodeURIComponent(state.host)}&sz=64`;
      fav.addEventListener("error", () => {
        fav.hidden = true;
      });
      domainRow.append(fav);
    }
    domainRow.append(el("div", "omni-shield-domain", state.host || "New Tab"));
    site.append(domainRow);
    const status = el("div", "omni-shield-status");
    status.innerHTML = state.host
      ? `Shields <strong>${state.siteShieldsUp ? "up" : "down"}</strong> for this site`
      : `Shields <strong>${state.siteShieldsUp ? "up" : "down"}</strong>`;
    site.append(status);
    head.append(site);

    const siteToggle = el("button", "omni-shield-toggle");
    siteToggle.type = "button";
    siteToggle.setAttribute("aria-label", "Toggle shields");
    if (state.siteShieldsUp) {
      siteToggle.classList.add("is-on");
    }
    siteToggle.addEventListener("click", async (event) => {
      event.preventDefault();
      event.stopPropagation();
      const next = await applyShieldAction("toggle-site", state);
      fillShields(panelNode, {
        ...normalizeShieldData(next, state),
        pageUrl: state.pageUrl,
        host: state.host || next.host || "",
      });
      requestAnimationFrame(reportSize);
    });
    head.append(siteToggle);
    panelNode.append(head);

    const stat = el("div", "omni-shield-stat");
    const count = el("span", "omni-shield-stat-count", formatShieldCount(state.blockedForHost));
    stat.append(count);
    stat.append(el("span", "omni-shield-stat-label", "trackers, ads, and more blocked"));
    panelNode.append(stat);

    panelNode.append(el("div", "omni-shield-divider"));

    const advBtn = el("button", "omni-shield-advanced-btn");
    advBtn.type = "button";
    advBtn.setAttribute("aria-expanded", shieldsAdvancedOpen ? "true" : "false");
    const gear = el("span", "omni-shield-icon");
    gear.innerHTML = ICON_GEAR;
    const chevron = el("span", "omni-shield-chevron");
    chevron.innerHTML = ICON_CHEVRON;
    advBtn.append(gear, el("span", "", "Advanced options"), chevron);
    panelNode.append(advBtn);

    const adv = el("div", "omni-shield-advanced");
    if (!shieldsAdvancedOpen) {
      adv.hidden = true;
    }

    function makeRow(label, isOn, action) {
      const row = el("div", "omni-shield-advanced-row");
      row.append(el("span", "", label));
      const t = el("button", "omni-shield-toggle");
      t.type = "button";
      t.setAttribute("aria-label", label);
      if (isOn) {
        t.classList.add("is-on");
      }
      t.addEventListener("click", async (event) => {
        event.preventDefault();
        event.stopPropagation();
        const next = await applyShieldAction(action, state);
        fillShields(panelNode, {
          ...normalizeShieldData(next, state),
          pageUrl: state.pageUrl,
          host: state.host || next.host || "",
        });
        requestAnimationFrame(reportSize);
      });
      row.append(t);
      return row;
    }

    adv.append(makeRow("Aggressive blocking", state.aggressive, "toggle-aggressive"));
    adv.append(makeRow("Block ads globally", state.enabled, "toggle-global"));
    panelNode.append(adv);

    advBtn.addEventListener("click", (event) => {
      event.preventDefault();
      event.stopPropagation();
      shieldsAdvancedOpen = !shieldsAdvancedOpen;
      adv.hidden = !shieldsAdvancedOpen;
      advBtn.setAttribute("aria-expanded", shieldsAdvancedOpen ? "true" : "false");
      requestAnimationFrame(reportSize);
    });

    const foot = el("div", "omni-shield-foot");
    foot.append(
      document.createTextNode(
        "If this site seems broken, try Shields down. This may reduce Omni Browser’s privacy protections. "
      )
    );
    const learn = el("a", "omni-shield-learn", "Learn more");
    learn.href = "#";
    learn.addEventListener("click", (event) => {
      event.preventDefault();
      event.stopPropagation();
      sendCommand({ action: "open-info" });
    });
    foot.append(learn);
    panelNode.append(foot);

    panelNode._shieldState = state;
  }

  function patchShields(data) {
    if (!panel || !panel.classList.contains("omni-shield")) {
      return false;
    }
    const prev = panel._shieldState || {};
    const next = normalizeShieldData(data, prev);
    // Keep advanced open state; rebuild for reliable toggle wiring.
    fillShields(panel, next);
    return true;
  }

  function renderShields(payload) {
    document.body.classList.remove("is-tab-tip", "is-history", "is-media");
    document.body.classList.add("is-shields");
    const data = payload || {};
    if (panel && panel.classList.contains("omni-shield")) {
      patchShields(data);
      requestAnimationFrame(reportSize);
      return;
    }
    lastReportW = 0;
    lastReportH = 0;
    shieldsAdvancedOpen = Boolean(data.advancedOpen);
    panel = el("div", "omni-shield");
    layout = panel;
    panel.setAttribute("role", "dialog");
    panel.setAttribute("aria-label", "Ad blocking");
    fillShields(panel, data);
    root.replaceChildren(panel);
    observeSize();
    requestAnimationFrame(reportSize);
  }

  let findInput = null;
  let findCount = null;
  let findPrevBtn = null;
  let findNextBtn = null;
  let findCloseBtn = null;
  let findQuery = "";

  function updateFindResult(count, active) {
    if (!findCount) return;
    if (!findQuery) {
      findCount.textContent = "0/0";
      findCount.className = "omni-find-count";
      return;
    }
    if (count <= 0) {
      findCount.textContent = "0/0";
      findCount.className = "omni-find-count no-matches";
    } else {
      findCount.textContent = `${active}/${count}`;
      findCount.className = "omni-find-count has-matches";
    }
  }

  function executeFind(query, findNext = false, forward = true) {
    findQuery = (query || "").trim();
    if (!window.OmniBridge || typeof window.OmniBridge.browserFind !== "function") return;
    if (!findQuery) {
      window.OmniBridge.browserStopFinding(true).catch(() => {});
      if (findCount) {
        findCount.textContent = "0/0";
        findCount.className = "omni-find-count";
      }
      return;
    }
    window.OmniBridge.browserFind(findQuery, forward, false, findNext).catch(() => {});
  }

  function closeFind() {
    if (window.OmniBridge && typeof window.OmniBridge.browserStopFinding === "function") {
      window.OmniBridge.browserStopFinding(true).catch(() => {});
    }
    if (window.OmniBridge && typeof window.OmniBridge.overlayHide === "function") {
      window.OmniBridge.overlayHide().catch(() => {});
    }
  }

  function renderFind(payload) {
    document.body.classList.remove("is-tab-tip", "is-history", "is-media", "is-shields");
    document.body.classList.add("is-find");
    const data = payload || {};
    findQuery = data.query || "";

    const bar = el("div", "omni-find-bar");
    bar.setAttribute("role", "search");
    bar.setAttribute("aria-label", "Find in page");

    const iconWrap = el("span", "omni-find-icon");
    iconWrap.innerHTML = '<svg xmlns="http://www.w3.org/2000/svg" width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/></svg>';

    findInput = el("input", "omni-find-input");
    findInput.type = "text";
    findInput.placeholder = "Find in page...";
    findInput.value = findQuery;
    findInput.autocomplete = "off";
    findInput.spellcheck = false;

    findCount = el("span", "omni-find-count", "0/0");

    findPrevBtn = el("button", "omni-find-btn");
    findPrevBtn.innerHTML = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="m18 15-6-6-6 6"/></svg>';
    findPrevBtn.type = "button";
    findPrevBtn.title = "Previous (Shift+Enter)";
    findPrevBtn.setAttribute("aria-label", "Previous match");

    findNextBtn = el("button", "omni-find-btn");
    findNextBtn.innerHTML = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="m6 9 6 6 6-6"/></svg>';
    findNextBtn.type = "button";
    findNextBtn.title = "Next (Enter)";
    findNextBtn.setAttribute("aria-label", "Next match");

    findCloseBtn = el("button", "omni-find-btn omni-find-close");
    findCloseBtn.innerHTML = '<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M18 6 6 18"/><path d="m6 6 12 12"/></svg>';
    findCloseBtn.type = "button";
    findCloseBtn.title = "Close (Escape)";
    findCloseBtn.setAttribute("aria-label", "Close find bar");

    bar.append(iconWrap, findInput, findCount, findPrevBtn, findNextBtn, findCloseBtn);

    panel = bar;
    layout = bar;
    root.replaceChildren(bar);

    findInput.addEventListener("input", () => {
      executeFind(findInput.value, false, true);
    });

    findInput.addEventListener("keydown", (e) => {
      if (e.key === "Enter") {
        e.preventDefault();
        executeFind(findInput.value, true, !e.shiftKey);
      } else if (e.key === "Escape") {
        e.preventDefault();
        closeFind();
      } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "f") {
        e.preventDefault();
        closeFind();
      }
    });

    findPrevBtn.addEventListener("click", () => {
      executeFind(findInput.value, true, false);
    });

    findNextBtn.addEventListener("click", () => {
      executeFind(findInput.value, true, true);
    });

    findCloseBtn.addEventListener("click", () => {
      closeFind();
    });

    setTimeout(() => {
      if (findInput) {
        findInput.focus();
        findInput.select();
      }
    }, 20);

    if (findQuery) {
      executeFind(findQuery, false, true);
    }
  }

  function clear() {
    if (resizeObserver) {
      resizeObserver.disconnect();
      resizeObserver = null;
    }
    panel = null;
    layout = null;
    lastReportW = 0;
    lastReportH = 0;
    findInput = null;
    findCount = null;
    findPrevBtn = null;
    findNextBtn = null;
    findCloseBtn = null;
    document.body.classList.remove(
      "is-tab-tip",
      "is-history",
      "is-media",
      "is-shields",
      "is-update",
      "is-find"
    );
    root.replaceChildren();
  }

  function onOverlayEvent(msg) {
    if (!msg || !msg.type) {
      return;
    }
    if (msg.type === "show") {
      const payload = msg.payload || {};
      if (payload.view === "tab-tip") {
        renderTabTip(payload);
      } else if (payload.view === "history") {
        renderHistory(payload);
      } else if (payload.view === "media") {
        renderMedia(payload);
      } else if (payload.view === "update") {
        renderUpdate(payload);
      } else if (payload.view === "shields") {
        renderShields(payload);
      } else if (payload.view === "find") {
        renderFind(payload);
      }
    }
    if (msg.type === "find-result") {
      updateFindResult(msg.count, msg.active);
    }
    if (msg.type === "hide") {
      clear();
    }
  }

  function boot() {
    if (
      !window.OmniBridge ||
      typeof OmniBridge.overlaySubscribe !== "function" ||
      typeof window.cefQuery !== "function"
    ) {
      return;
    }
    OmniBridge.overlaySubscribe((msg, err) => {
      if (!err) {
        onOverlayEvent(msg);
      }
    }).catch(() => {});
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", boot);
  } else {
    boot();
  }
})();
