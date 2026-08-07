(() => {
  const FALLBACK_FAVICON =
    "data:image/svg+xml," +
    encodeURIComponent(
      '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16"><rect width="16" height="16" rx="2" fill="#3a393a"/><circle cx="8" cy="8" r="3" fill="#8a8a8a"/></svg>'
    );

  const listEl = document.getElementById("hist-list");
  const emptyEl = document.getElementById("hist-empty");
  const searchForm = document.getElementById("hist-search");
  const searchInput = document.getElementById("hist-q");
  const removeSelected = document.getElementById("hist-remove-selected");
  const actionsEl = document.getElementById("hist-actions");
  const navOmni = document.getElementById("hist-nav-omni");
  const navClear = document.getElementById("hist-nav-clear");

  /** @type {{ url: string, title: string, ts: number }[]} */
  let entries = [];
  let query = "";
  /** @type {HTMLElement|null} */
  let openMenu = null;
  /** @type {number} */
  let lastCheckedIndex = -1;

  function allCheckboxes() {
    return Array.from(
      listEl.querySelectorAll('input[type="checkbox"][data-url]')
    );
  }

  function syncSelectionUi() {
    const checked = allCheckboxes().filter((el) => el.checked);
    if (actionsEl) {
      actionsEl.hidden = checked.length === 0;
    }
  }

  function onCheckboxClick(event) {
    const check = event.currentTarget;
    if (!(check instanceof HTMLInputElement)) {
      return;
    }
    const boxes = allCheckboxes();
    const index = boxes.indexOf(check);
    if (index < 0) {
      return;
    }

    if (event.shiftKey && lastCheckedIndex >= 0 && lastCheckedIndex !== index) {
      event.preventDefault();
      const from = Math.min(lastCheckedIndex, index);
      const to = Math.max(lastCheckedIndex, index);
      for (let i = from; i <= to; i += 1) {
        boxes[i].checked = true;
      }
      lastCheckedIndex = index;
      syncSelectionUi();
      return;
    }

    lastCheckedIndex = index;
  }

  function nativeQuery(method, params = {}) {
    return new Promise((resolve, reject) => {
      if (typeof window.cefQuery !== "function") {
        reject(new Error("Native bridge unavailable"));
        return;
      }
      window.cefQuery({
        request: JSON.stringify({ method, params }),
        persistent: false,
        onSuccess(response) {
          try {
            resolve(response ? JSON.parse(response) : null);
          } catch (err) {
            reject(err);
          }
        },
        onFailure(code, message) {
          reject(new Error(message || `Native error ${code}`));
        },
      });
    });
  }

  function faviconCandidates(url) {
    try {
      const parsed = new URL(url);
      if (parsed.protocol !== "http:" && parsed.protocol !== "https:") {
        return [FALLBACK_FAVICON];
      }
      const host = parsed.hostname;
      const bare = host.replace(/^www\./, "");
      return [
        `${parsed.origin}/favicon.ico`,
        `https://www.google.com/s2/favicons?domain=${encodeURIComponent(bare)}&sz=64`,
        `https://www.google.com/s2/favicons?domain=${encodeURIComponent(host)}&sz=64`,
        FALLBACK_FAVICON,
      ];
    } catch (_) {
      return [FALLBACK_FAVICON];
    }
  }

  function bindFavicon(img, url) {
    const list = faviconCandidates(url);
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

  function parseTs(ts) {
    const n = Number(ts);
    if (!Number.isFinite(n) || n <= 0) {
      return null;
    }
    // Support accidental seconds timestamps.
    const ms = n < 1e12 ? n * 1000 : n;
    const d = new Date(ms);
    return Number.isNaN(d.getTime()) ? null : d;
  }

  function startOfLocalDay(date) {
    return new Date(date.getFullYear(), date.getMonth(), date.getDate());
  }

  function dayKey(ts) {
    const d = parseTs(ts);
    if (!d) {
      return "unknown";
    }
    const y = d.getFullYear();
    const m = String(d.getMonth() + 1).padStart(2, "0");
    const day = String(d.getDate()).padStart(2, "0");
    return `${y}-${m}-${day}`;
  }

  function dayLabel(ts) {
    const d = parseTs(ts);
    if (!d) {
      return "Unknown date";
    }
    const entryDay = startOfLocalDay(d).getTime();
    const today = startOfLocalDay(new Date()).getTime();
    const yesterdayDate = new Date();
    yesterdayDate.setDate(yesterdayDate.getDate() - 1);
    const yesterday = startOfLocalDay(yesterdayDate).getTime();
    if (entryDay === today) {
      return "Today";
    }
    if (entryDay === yesterday) {
      return "Yesterday";
    }
    return d.toLocaleDateString("en-US", {
      month: "long",
      day: "numeric",
      year: "numeric",
    });
  }

  function timeLabel(ts) {
    const d = parseTs(ts);
    if (!d) {
      return "";
    }
    return d.toLocaleTimeString(undefined, {
      hour: "numeric",
      minute: "2-digit",
    });
  }

  function filteredEntries() {
    const q = query.trim().toLowerCase();
    if (!q) {
      return entries.slice();
    }
    return entries.filter((entry) => {
      const title = String(entry.title || "").toLowerCase();
      const url = String(entry.url || "").toLowerCase();
      return title.includes(q) || url.includes(q);
    });
  }

  function closeMenu() {
    if (openMenu) {
      openMenu.remove();
      openMenu = null;
    }
    document.querySelectorAll(".hist-more[aria-expanded='true']").forEach((btn) => {
      btn.setAttribute("aria-expanded", "false");
    });
  }

  function selectedUrls() {
    return Array.from(
      listEl.querySelectorAll('input[type="checkbox"][data-url]:checked')
    ).map((el) => el.getAttribute("data-url"));
  }

  function render() {
    closeMenu();
    lastCheckedIndex = -1;
    const items = filteredEntries();
    listEl.replaceChildren();
    if (!items.length) {
      emptyEl.hidden = false;
      emptyEl.textContent = query.trim()
        ? "No matching history."
        : "No history yet.";
      syncSelectionUi();
      return;
    }
    emptyEl.hidden = true;

    /** @type {HTMLElement|null} */
    let dayBox = null;
    /** @type {HTMLElement|null} */
    let dayRows = null;
    let lastDay = "";

    items.forEach((entry) => {
      const key = dayKey(entry.ts);
      if (key !== lastDay) {
        lastDay = key;
        dayBox = document.createElement("section");
        dayBox.className = "hist-day-box";

        const heading = document.createElement("h2");
        heading.className = "hist-day";
        heading.textContent = dayLabel(entry.ts);
        dayBox.append(heading);

        dayRows = document.createElement("div");
        dayRows.className = "hist-day-rows";
        dayBox.append(dayRows);
        listEl.append(dayBox);
      }

      const row = document.createElement("div");
      row.className = "hist-row";

      const check = document.createElement("input");
      check.type = "checkbox";
      check.setAttribute("data-url", entry.url);
      check.addEventListener("click", onCheckboxClick);
      check.addEventListener("change", syncSelectionUi);

      const time = document.createElement("span");
      time.className = "hist-time";
      time.textContent = timeLabel(entry.ts);

      const icon = document.createElement("img");
      icon.className = "hist-favicon";
      icon.alt = "";
      icon.draggable = false;
      bindFavicon(icon, entry.url);

      const copy = document.createElement("div");
      copy.className = "hist-copy";
      const link = document.createElement("a");
      link.className = "hist-link";
      link.href = entry.url;
      link.textContent = entry.title || entry.url;
      link.addEventListener("click", (event) => {
        event.preventDefault();
        nativeQuery("browser.navigate", { url: entry.url }).catch(() => {
          window.location.href = entry.url;
        });
      });
      copy.append(link);

      const more = document.createElement("button");
      more.type = "button";
      more.className = "hist-more";
      more.setAttribute("aria-label", "More actions");
      more.setAttribute("aria-expanded", "false");
      more.textContent = "⋮";
      more.addEventListener("click", (event) => {
        event.stopPropagation();
        const already = more.getAttribute("aria-expanded") === "true";
        closeMenu();
        if (already) {
          return;
        }
        const menu = document.createElement("div");
        menu.className = "hist-menu";
        menu.setAttribute("role", "menu");
        const removeBtn = document.createElement("button");
        removeBtn.type = "button";
        removeBtn.textContent = "Remove from history";
        removeBtn.addEventListener("click", async (ev) => {
          ev.stopPropagation();
          closeMenu();
          await removeUrls([entry.url]);
        });
        menu.append(removeBtn);
        document.body.append(menu);
        const rect = more.getBoundingClientRect();
        menu.style.left = `${Math.max(8, rect.right - 180)}px`;
        menu.style.top = `${rect.bottom + 4}px`;
        openMenu = menu;
        more.setAttribute("aria-expanded", "true");
      });

      row.append(check, time, icon, copy, more);
      dayRows.append(row);
    });
    syncSelectionUi();
  }

  async function loadEntries() {
    try {
      const result = await nativeQuery("history.list", {});
      entries = Array.isArray(result && result.entries) ? result.entries : [];
    } catch (_) {
      entries = [];
    }
    render();
  }

  async function removeUrls(urls) {
    const unique = Array.from(new Set(urls.filter(Boolean)));
    for (const url of unique) {
      try {
        await nativeQuery("history.remove", { url });
      } catch (_) {
        /* ignore */
      }
    }
    await loadEntries();
  }

  searchForm?.addEventListener("submit", (event) => {
    event.preventDefault();
  });

  searchInput?.addEventListener("input", () => {
    query = searchInput.value || "";
    render();
  });

  removeSelected?.addEventListener("click", async () => {
    await removeUrls(selectedUrls());
  });

  navOmni?.addEventListener("click", () => {
    navOmni.classList.add("is-active");
    navClear?.classList.remove("is-active");
  });

  navClear?.addEventListener("click", async () => {
    navClear.classList.add("is-active");
    navOmni?.classList.remove("is-active");
    const ok = window.confirm("Delete all browsing history?");
    if (!ok) {
      navOmni?.classList.add("is-active");
      navClear.classList.remove("is-active");
      return;
    }
    try {
      await nativeQuery("history.clear", {});
    } catch (_) {
      /* ignore */
    }
    navOmni?.classList.add("is-active");
    navClear.classList.remove("is-active");
    await loadEntries();
  });

  document.addEventListener("click", closeMenu);
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      closeMenu();
    }
  });

  loadEntries();
})();
