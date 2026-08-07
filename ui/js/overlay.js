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
    document.body.classList.remove("is-history", "is-media");
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
    document.body.classList.remove("is-tab-tip", "is-history");
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
    document.body.classList.remove("is-tab-tip", "is-media");
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

  function clear() {
    if (resizeObserver) {
      resizeObserver.disconnect();
      resizeObserver = null;
    }
    panel = null;
    layout = null;
    lastReportW = 0;
    lastReportH = 0;
    document.body.classList.remove("is-tab-tip", "is-history", "is-media");
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
      }
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
