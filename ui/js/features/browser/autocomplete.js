(() => {
  const TOP_DOMAINS = [
    "youtube.com",
    "google.com",
    "github.com",
    "reddit.com",
    "wikipedia.org",
    "amazon.de",
    "amazon.com",
    "twitter.com",
    "x.com",
    "twitch.tv",
    "netflix.com",
    "chatgpt.com",
    "openai.com",
    "discord.com",
    "spotify.com",
    "instagram.com",
    "facebook.com",
    "tiktok.com",
    "linkedin.com",
    "stackoverflow.com",
    "ebay.de",
    "ebay.com",
    "duckduckgo.com",
    "pinterest.com",
    "microsoft.com",
    "apple.com",
    "yahoo.com",
    "outlook.com",
    "gmail.com",
    "bing.com",
    "imdb.com",
    "fandom.com",
    "steampowered.com",
    "roblox.com",
    "cnn.com",
    "bbc.com",
    "spiegel.de",
    "tagesschau.de",
    "zeit.de",
    "heise.de",
    "golem.de",
    "wetter.com",
    "translate.google.com",
    "maps.google.com",
    "whatsapp.com",
    "telegram.org",
    "signal.org",
    "medium.com",
    "dev.to",
    "gitlab.com",
    "bitbucket.org",
    "npm.js.org",
    "npmjs.com",
    "claude.ai",
    "gemini.google.com",
  ];

  const ICON_SEARCH =
    '<svg xmlns="http://www.w3.org/2000/svg" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg>';
  const ICON_GLOBE =
    '<svg xmlns="http://www.w3.org/2000/svg" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><circle cx="12" cy="12" r="10"/><line x1="2" y1="12" x2="22" y2="12"/><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"/></svg>';
  const ICON_HISTORY =
    '<svg xmlns="http://www.w3.org/2000/svg" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>';
  const ICON_BOOKMARK =
    '<svg xmlns="http://www.w3.org/2000/svg" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="m19 21-7-4-7 4V5a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2v16z"/></svg>';

  let activePopup = null;
  let activeInput = null;
  let activeIndex = -1;
  let currentSuggestions = [];
  let lastTyped = "";
  let isComposing = false;
  let suppressInline = false;
  let fetchTimer = 0;

  function cleanQuery(str) {
    return String(str || "").trim().toLowerCase();
  }

  function domainFromUrl(url) {
    try {
      const u = new URL(url.includes("://") ? url : `https://${url}`);
      return u.hostname.replace(/^www\./, "");
    } catch (_) {
      return url;
    }
  }

  function getHistoryAndBookmarkMatches(query) {
    const q = cleanQuery(query);
    if (!q) return [];
    const results = [];
    const seen = new Set();

    // Check bookmarks
    if (window.OmniBrowser && typeof OmniBrowser.listBookmarks === "function") {
      const bms = OmniBrowser.listBookmarks();
      for (const bm of bms) {
        if (!bm || !bm.url) continue;
        const dom = domainFromUrl(bm.url);
        const title = String(bm.title || "").toLowerCase();
        if (
          dom.toLowerCase().includes(q) ||
          bm.url.toLowerCase().includes(q) ||
          title.includes(q)
        ) {
          if (!seen.has(bm.url)) {
            seen.add(bm.url);
            results.push({
              type: "bookmark",
              title: bm.title || dom,
              value: bm.url,
              sub: bm.url,
              badge: "Bookmark",
              icon: ICON_BOOKMARK,
            });
          }
        }
      }
    }

    // Check history
    if (window.OmniBrowser && typeof OmniBrowser.listHistory === "function") {
      const hist = OmniBrowser.listHistory();
      for (const h of hist) {
        if (!h || !h.url) continue;
        const dom = domainFromUrl(h.url);
        const title = String(h.title || "").toLowerCase();
        if (
          dom.toLowerCase().includes(q) ||
          h.url.toLowerCase().includes(q) ||
          title.includes(q)
        ) {
          if (!seen.has(h.url)) {
            seen.add(h.url);
            results.push({
              type: "history",
              title: h.title || dom,
              value: h.url,
              sub: h.url,
              badge: "History",
              icon: ICON_HISTORY,
            });
          }
        }
      }
    }

    return results.slice(0, 4);
  }

  function getTopDomainMatches(query) {
    const q = cleanQuery(query).replace(/^https?:\/\//, "").replace(/^www\./, "");
    if (!q) return [];
    const matches = [];
    for (const domain of TOP_DOMAINS) {
      if (domain.startsWith(q)) {
        matches.push({
          type: "domain",
          title: domain,
          value: `https://${domain}`,
          sub: `https://${domain}`,
          badge: "Website",
          icon: ICON_GLOBE,
          inlineMatch: domain,
        });
      }
    }
    return matches.slice(0, 3);
  }

  async function fetchSearchSuggestions(query) {
    const q = cleanQuery(query);
    if (!q || q.length < 2) return [];
    try {
      const url = `https://duckduckgo.com/ac/?q=${encodeURIComponent(q)}&type=list`;
      const res = await fetch(url, { signal: AbortSignal.timeout(600) });
      if (!res.ok) return [];
      const data = await res.json();
      if (Array.isArray(data) && Array.isArray(data[1])) {
        return data[1].slice(0, 5).map((item) => ({
          type: "search",
          title: String(item),
          value: String(item),
          sub: "Search with default engine",
          badge: "Search",
          icon: ICON_SEARCH,
        }));
      }
    } catch (_) {
      // Offline or blocked
    }
    return [];
  }

  function highlightMatch(text, query) {
    const q = cleanQuery(query);
    if (!q) return escapeHtml(text);
    const lower = text.toLowerCase();
    const idx = lower.indexOf(q);
    if (idx === -1) return escapeHtml(text);
    return (
      escapeHtml(text.slice(0, idx)) +
      "<strong>" +
      escapeHtml(text.slice(idx, idx + q.length)) +
      "</strong>" +
      escapeHtml(text.slice(idx + q.length))
    );
  }

  function escapeHtml(str) {
    return String(str)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;");
  }

  function ensurePopup(input) {
    let popup = input.form ? input.form.querySelector(".omni-autocomplete-popup") : null;
    if (!popup) {
      const parent = input.closest(".browser-start-search-wrap") || input.form || input.parentElement;
      popup = parent.querySelector(".omni-autocomplete-popup");
      if (!popup) {
        popup = document.createElement("div");
        popup.className = "omni-autocomplete-popup";
        popup.hidden = true;
        popup.setAttribute("role", "listbox");
        parent.style.position = "relative";
        parent.appendChild(popup);
      }
    }
    return popup;
  }

  function closePopup() {
    if (activePopup) {
      activePopup.hidden = true;
      activePopup.replaceChildren();
      activePopup = null;
    }
    activeIndex = -1;
    currentSuggestions = [];
  }

  function renderPopup(input, suggestions, query) {
    if (!suggestions || !suggestions.length) {
      closePopup();
      return;
    }

    const popup = ensurePopup(input);
    activePopup = popup;
    activeInput = input;
    currentSuggestions = suggestions;
    activeIndex = -1;
    popup.replaceChildren();

    suggestions.forEach((item, index) => {
      const row = document.createElement("div");
      row.className = "omni-autocomplete-item";
      row.setAttribute("role", "option");
      row.dataset.index = String(index);

      const icon = document.createElement("span");
      icon.className = "omni-autocomplete-icon";
      icon.innerHTML = item.icon;

      const content = document.createElement("div");
      content.className = "omni-autocomplete-content";

      const title = document.createElement("div");
      title.className = "omni-autocomplete-title";
      title.innerHTML = highlightMatch(item.title, query);

      const sub = document.createElement("div");
      sub.className = "omni-autocomplete-sub";
      sub.textContent = item.sub || item.value;

      content.append(title, sub);

      const badge = document.createElement("span");
      badge.className = "omni-autocomplete-badge";
      badge.textContent = item.badge || "Search";

      row.append(icon, content, badge);

      row.addEventListener("mousedown", (e) => {
        e.preventDefault();
        selectSuggestion(item);
      });

      popup.appendChild(row);
    });

    popup.hidden = false;
  }

  function selectSuggestion(item) {
    if (!item) return;
    closePopup();
    if (window.OmniBrowser && typeof OmniBrowser.navigate === "function") {
      OmniBrowser.navigate(item.value);
    } else if (activeInput && activeInput.form) {
      activeInput.value = item.value;
      activeInput.form.dispatchEvent(new Event("submit", { cancelable: true, bubbles: true }));
    }
  }

  function updateSelectionVisuals() {
    if (!activePopup) return;
    const items = activePopup.querySelectorAll(".omni-autocomplete-item");
    items.forEach((el, i) => {
      const isSelected = i === activeIndex;
      el.classList.toggle("is-selected", isSelected);
      el.setAttribute("aria-selected", isSelected ? "true" : "false");
      if (isSelected) {
        el.scrollIntoView({ block: "nearest" });
      }
    });
  }

  function handleInlineCompletion(input, query, domainMatches) {
    if (suppressInline || isComposing) return;
    const q = cleanQuery(query).replace(/^https?:\/\//, "").replace(/^www\./, "");
    if (!q || q.length < 1) return;

    // Find best match that starts with q
    let matchDomain = "";
    if (domainMatches && domainMatches.length && domainMatches[0].inlineMatch) {
      matchDomain = domainMatches[0].inlineMatch;
    }

    if (matchDomain && matchDomain.toLowerCase().startsWith(q.toLowerCase())) {
      const origLen = query.length;
      // Preserve user typed casing/prefix if any
      const fullValue = query + matchDomain.slice(q.length);
      input.value = fullValue;
      input.setSelectionRange(origLen, fullValue.length);
    }
  }

  async function updateSuggestions(input) {
    const rawVal = input.value;
    const query = rawVal.trim();
    if (!query) {
      closePopup();
      return;
    }

    const domainMatches = getTopDomainMatches(query);
    const histMatches = getHistoryAndBookmarkMatches(query);

    // Provide immediate results from domains and history
    const immediate = [...domainMatches, ...histMatches];
    if (immediate.length > 0) {
      renderPopup(input, immediate, query);
    }

    // Try inline autocomplete for domains
    handleInlineCompletion(input, rawVal, domainMatches);

    // Fetch async search suggestions
    window.clearTimeout(fetchTimer);
    fetchTimer = window.setTimeout(async () => {
      if (input !== document.activeElement || input.value.trim() !== query) {
        return;
      }
      const searchMatches = await fetchSearchSuggestions(query);
      const combined = [...domainMatches, ...histMatches, ...searchMatches];
      if (combined.length > 0 && input === document.activeElement) {
        renderPopup(input, combined, query);
      }
    }, 90);
  }

  function bindInput(input) {
    if (!input) return;

    input.addEventListener("compositionstart", () => {
      isComposing = true;
    });

    input.addEventListener("compositionend", () => {
      isComposing = false;
      updateSuggestions(input);
    });

    input.addEventListener("input", (e) => {
      if (e.inputType === "deleteContentBackward" || e.inputType === "deleteContentForward") {
        suppressInline = true;
      } else {
        suppressInline = false;
      }
      lastTyped = input.value;
      updateSuggestions(input);
    });

    input.addEventListener("keydown", (e) => {
      if (e.key === "Tab") {
        // If there's an inline selection or an active popup item, accept it!
        const hasInline =
          input.selectionStart !== input.selectionEnd &&
          input.selectionEnd === input.value.length;

        if (hasInline) {
          e.preventDefault();
          input.setSelectionRange(input.value.length, input.value.length);
          closePopup();
          return;
        }

        if (activePopup && !activePopup.hidden && currentSuggestions.length > 0) {
          e.preventDefault();
          if (activeIndex >= 0 && activeIndex < currentSuggestions.length) {
            input.value = currentSuggestions[activeIndex].value;
          } else {
            input.value = currentSuggestions[0].value;
          }
          input.setSelectionRange(input.value.length, input.value.length);
          closePopup();
          return;
        }
      }

      if (e.key === "ArrowRight") {
        const hasInline =
          input.selectionStart !== input.selectionEnd &&
          input.selectionEnd === input.value.length;
        if (hasInline) {
          input.setSelectionRange(input.value.length, input.value.length);
        }
      }

      if (e.key === "ArrowDown") {
        if (activePopup && !activePopup.hidden && currentSuggestions.length > 0) {
          e.preventDefault();
          activeIndex = (activeIndex + 1) % currentSuggestions.length;
          updateSelectionVisuals();
          input.value = currentSuggestions[activeIndex].value;
        }
      }

      if (e.key === "ArrowUp") {
        if (activePopup && !activePopup.hidden && currentSuggestions.length > 0) {
          e.preventDefault();
          activeIndex = (activeIndex - 1 + currentSuggestions.length) % currentSuggestions.length;
          updateSelectionVisuals();
          input.value = currentSuggestions[activeIndex].value;
        }
      }

      if (e.key === "Enter") {
        if (activePopup && !activePopup.hidden && activeIndex >= 0 && currentSuggestions[activeIndex]) {
          e.preventDefault();
          selectSuggestion(currentSuggestions[activeIndex]);
          return;
        }
        closePopup();
      }

      if (e.key === "Escape") {
        if (activePopup && !activePopup.hidden) {
          e.preventDefault();
          closePopup();
        }
      }
    });

    input.addEventListener("focus", () => {
      if (input.value.trim()) {
        updateSuggestions(input);
      }
    });

    input.addEventListener("blur", () => {
      // Delay closing to allow clicking suggestions
      window.setTimeout(() => {
        closePopup();
      }, 180);
    });
  }

  function boot() {
    const searchInput = document.getElementById("browser-search-input");
    const startInput = document.getElementById("browser-start-search-input");

    if (searchInput) {
      bindInput(searchInput);
    }
    if (startInput) {
      bindInput(startInput);
    }

    document.addEventListener("click", (e) => {
      if (activePopup && !activePopup.contains(e.target) && e.target !== activeInput) {
        closePopup();
      }
    });
  }

  window.OmniAutocomplete = {
    boot,
    close: closePopup,
  };
})();
