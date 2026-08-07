(() => {
  let tip = null;
  let active = null;
  let showTimer = null;
  let richMode = false;
  let richToken = 0;

  const ICON_VOLUME =
    '<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"/><path d="M15.54 8.46a5 5 0 0 1 0 7.07"/><path d="M19.07 4.93a10 10 0 0 1 0 14.14"/></svg>';
  const ICON_VOLUME_OFF =
    '<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"/><line x1="22" x2="16" y1="9" y2="15"/><line x1="16" x2="22" y1="9" y2="15"/></svg>';
  const ICON_GAUGE =
    '<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="m12 14 4-4"/><path d="M3.34 19a10 10 0 1 1 17.32 0"/></svg>';

  function ensureTip() {
    if (tip) {
      return tip;
    }
    tip = document.createElement("div");
    tip.className = "omni-tooltip";
    tip.setAttribute("role", "tooltip");
    tip.hidden = true;
    document.body.appendChild(tip);
    return tip;
  }

  function hide() {
    if (showTimer) {
      clearTimeout(showTimer);
      showTimer = null;
    }
    active = null;
    richMode = false;
    richToken += 1;
    if (!tip) {
      return;
    }
    tip.classList.remove("is-visible", "is-bottom", "is-top", "is-rich");
    tip.hidden = true;
    tip.replaceChildren();
    tip.style.left = "";
    tip.style.top = "";
  }

  function placeSimple(el) {
    const node = ensureTip();
    const rect = el.getBoundingClientRect();
    const gap = 8;
    const placement = el.getAttribute("data-tooltip-placement") || "top";
    node.textContent = el.getAttribute("data-tooltip") || "";
    node.hidden = false;
    node.classList.remove("is-bottom", "is-top", "is-rich");
    node.classList.add(placement === "bottom" ? "is-bottom" : "is-top");
    node.classList.add("is-visible");

    const tipRect = node.getBoundingClientRect();
    let left = rect.left + rect.width / 2;
    let top =
      placement === "bottom"
        ? rect.bottom + gap
        : rect.top - tipRect.height - gap;

    const pad = 8;
    const half = tipRect.width / 2;
    left = Math.max(pad + half, Math.min(left, window.innerWidth - pad - half));

    if (placement === "top" && top < pad) {
      top = rect.bottom + gap;
      node.classList.remove("is-top");
      node.classList.add("is-bottom");
    } else if (
      placement === "bottom" &&
      top + tipRect.height > window.innerHeight - pad
    ) {
      top = rect.top - tipRect.height - gap;
      node.classList.remove("is-bottom");
      node.classList.add("is-top");
    }

    node.style.left = `${left}px`;
    node.style.top = `${top}px`;
  }

  function buildTabTipHtml({ title, domain, audioPlaying, audioMuted, memoryMb }) {
    const head = document.createElement("div");
    head.className = "omni-tab-tip-head";

    const titleEl = document.createElement("div");
    titleEl.className = "omni-tab-tip-title";
    titleEl.textContent = title || "New Tab";
    head.appendChild(titleEl);

    if (domain) {
      const domainEl = document.createElement("div");
      domainEl.className = "omni-tab-tip-domain";
      domainEl.textContent = domain;
      head.appendChild(domainEl);
    }

    const rows = [];
    if (audioPlaying) {
      rows.push({
        icon: audioMuted ? ICON_VOLUME_OFF : ICON_VOLUME,
        text: audioMuted ? "This tab is muted" : "This tab is playing audio",
      });
    }
    // Memory is always the last row.
    rows.push({
      icon: ICON_GAUGE,
      text:
        typeof memoryMb === "number" && Number.isFinite(memoryMb)
          ? `Memory usage: ${Math.max(0, Math.round(memoryMb))} MB`
          : "Memory usage: …",
    });

    const frag = document.createDocumentFragment();
    frag.appendChild(head);

    const meta = document.createElement("div");
    meta.className = "omni-tab-tip-meta";
    rows.forEach((row) => {
      const line = document.createElement("div");
      line.className = "omni-tab-tip-row";
      line.innerHTML = row.icon;
      const label = document.createElement("span");
      label.textContent = row.text;
      line.appendChild(label);
      meta.appendChild(line);
    });
    frag.appendChild(meta);

    return frag;
  }

  function placeRich(el) {
    const node = ensureTip();
    const rect = el.getBoundingClientRect();
    const gap = 6;
    node.hidden = false;
    node.classList.remove("is-top");
    node.classList.add("is-rich", "is-bottom", "is-visible");

    // Force layout so width/height are accurate before positioning.
    const tipRect = node.getBoundingClientRect();
    const pad = 8;
    let left = rect.left;
    // Always anchor below the tab (Chrome hover-card behavior).
    const top = rect.bottom + gap;

    if (left + tipRect.width > window.innerWidth - pad) {
      left = window.innerWidth - pad - tipRect.width;
    }
    left = Math.max(pad, left);

    node.style.left = `${left}px`;
    node.style.top = `${top}px`;
  }

  function show(el) {
    if (!el || !el.getAttribute("data-tooltip")) {
      return;
    }
    richMode = false;
    active = el;
    if (showTimer) {
      clearTimeout(showTimer);
    }
    showTimer = setTimeout(() => {
      if (active === el && !richMode) {
        placeSimple(el);
      }
    }, 280);
  }

  /**
   * Chrome-style tab hover card under a tab button.
   * @param {HTMLElement} el
   * @param {{ title: string, domain?: string, audioPlaying?: boolean, memoryMb?: number|null }} data
   * @returns {number} token for async updates
   */
  function showTab(el, data) {
    if (!el) {
      return 0;
    }
    richMode = true;
    active = el;
    const token = ++richToken;
    if (showTimer) {
      clearTimeout(showTimer);
      showTimer = null;
    }
    const node = ensureTip();
    node.replaceChildren(buildTabTipHtml(data || {}));
    placeRich(el);
    return token;
  }

  /**
   * Update an open tab tip (e.g. after memory is fetched).
   * @param {number} token
   * @param {HTMLElement} el
   * @param {{ title: string, domain?: string, audioPlaying?: boolean, memoryMb?: number|null }} data
   */
  function updateTab(token, el, data) {
    if (!richMode || token !== richToken || !tip || tip.hidden) {
      return;
    }
    if (el) {
      active = el;
    }
    if (!active) {
      return;
    }
    tip.replaceChildren(buildTabTipHtml(data || {}));
    placeRich(active);
  }

  function bootTooltips(root = document) {
    root.querySelectorAll("[data-tooltip]").forEach((el) => {
      if (el.dataset.tooltipBound === "1") {
        return;
      }
      el.dataset.tooltipBound = "1";
      el.addEventListener("pointerenter", () => show(el));
      el.addEventListener("pointerleave", hide);
      el.addEventListener("focus", () => show(el));
      el.addEventListener("blur", hide);
      el.addEventListener("click", hide);
    });
  }

  window.addEventListener("scroll", hide, true);
  window.addEventListener("resize", hide);

  window.OmniTooltip = {
    bootTooltips,
    hide,
    showTab,
    updateTab,
  };
})();
