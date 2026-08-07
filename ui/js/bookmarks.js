(() => {
  const FALLBACK_FAVICON =
    "data:image/svg+xml," +
    encodeURIComponent(
      '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16"><rect width="16" height="16" rx="2" fill="#3a393a"/><circle cx="8" cy="8" r="3" fill="#8a8a8a"/></svg>'
    );

  const CLOSE_SVG =
    '<svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true"><path d="M6 6l12 12M18 6L6 18" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>';

  const listEl = document.getElementById("bm-list");
  const emptyEl = document.getElementById("bm-empty");
  const searchForm = document.getElementById("bm-search");
  const searchInput = document.getElementById("bm-q");
  const navOmni = document.getElementById("bm-nav-omni");
  const navClear = document.getElementById("bm-nav-clear");

  /** @type {{ url: string, title: string, ts: number }[]} */
  let entries = [];
  let query = "";

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

  function render() {
    const items = filteredEntries();
    listEl.replaceChildren();
    if (!items.length) {
      emptyEl.hidden = false;
      emptyEl.textContent = query.trim()
        ? "No matching bookmarks."
        : "No bookmarks yet.";
      return;
    }
    emptyEl.hidden = true;

    const box = document.createElement("section");
    box.className = "hist-day-box";
    const rows = document.createElement("div");
    rows.className = "hist-day-rows";
    box.append(rows);
    listEl.append(box);

    items.forEach((entry) => {
      const row = document.createElement("div");
      row.className = "hist-row";

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

      const remove = document.createElement("button");
      remove.type = "button";
      remove.className = "hist-tool";
      remove.setAttribute("aria-label", "Remove bookmark");
      remove.title = "Remove bookmark";
      remove.innerHTML = CLOSE_SVG;
      remove.addEventListener("click", (event) => {
        event.preventDefault();
        event.stopPropagation();
        removeUrl(entry.url);
      });

      row.append(icon, copy, remove);
      rows.append(row);
    });
  }

  async function loadEntries() {
    try {
      const result = await nativeQuery("bookmarks.list", {});
      entries = Array.isArray(result && result.entries) ? result.entries : [];
    } catch (_) {
      entries = [];
    }
    render();
  }

  async function removeUrl(url) {
    if (!url) {
      return;
    }
    try {
      await nativeQuery("bookmarks.remove", { url });
    } catch (_) {
      /* ignore */
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

  navOmni?.addEventListener("click", () => {
    navOmni.classList.add("is-active");
    navClear?.classList.remove("is-active");
  });

  navClear?.addEventListener("click", async () => {
    navClear.classList.add("is-active");
    navOmni?.classList.remove("is-active");
    const ok = window.confirm("Delete all bookmarks?");
    if (!ok) {
      navOmni?.classList.add("is-active");
      navClear.classList.remove("is-active");
      return;
    }
    try {
      await nativeQuery("bookmarks.clear", {});
    } catch (_) {
      /* ignore */
    }
    navOmni?.classList.add("is-active");
    navClear.classList.remove("is-active");
    await loadEntries();
  });

  loadEntries();
})();
