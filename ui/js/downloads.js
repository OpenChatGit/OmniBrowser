(() => {
  const listEl = document.getElementById("dl-list");
  const emptyEl = document.getElementById("dl-empty");
  const searchForm = document.getElementById("dl-search");
  const searchInput = document.getElementById("dl-q");
  const navOmni = document.getElementById("dl-nav-omni");
  const navClear = document.getElementById("dl-nav-clear");

  /** @type {any[]} */
  let entries = [];
  let query = "";

  function callNative(method, params = {}) {
    if (!window.OmniBridge || typeof OmniBridge.call !== "function") {
      return Promise.reject(new Error("Native bridge unavailable"));
    }
    return OmniBridge.call(method, params);
  }

  function extensionOf(name) {
    const value = String(name || "");
    const idx = value.lastIndexOf(".");
    if (idx < 0 || idx === value.length - 1) {
      return "";
    }
    return value.slice(idx + 1).toLowerCase();
  }

  function fileKind(filename, mime) {
    const ext = extensionOf(filename);
    const m = String(mime || "").toLowerCase();
    if (
      ["png", "jpg", "jpeg", "gif", "webp", "svg", "bmp", "ico", "avif"].includes(
        ext
      ) ||
      m.startsWith("image/")
    ) {
      return "image";
    }
    if (
      ["mp4", "mkv", "webm", "mov", "avi", "m4v"].includes(ext) ||
      m.startsWith("video/")
    ) {
      return "video";
    }
    if (
      ["mp3", "wav", "flac", "ogg", "m4a", "aac"].includes(ext) ||
      m.startsWith("audio/")
    ) {
      return "audio";
    }
    if (["zip", "rar", "7z", "tar", "gz", "tgz", "bz2"].includes(ext)) {
      return "archive";
    }
    if (ext === "pdf" || m === "application/pdf") {
      return "pdf";
    }
    if (["exe", "msi", "dmg", "appx", "msix", "deb", "rpm"].includes(ext)) {
      return "app";
    }
    if (
      [
        "js",
        "ts",
        "tsx",
        "jsx",
        "py",
        "cpp",
        "c",
        "h",
        "cs",
        "go",
        "rs",
        "java",
        "html",
        "css",
        "json",
        "xml",
        "md",
      ].includes(ext) ||
      m.includes("javascript") ||
      m.includes("json")
    ) {
      return "code";
    }
    if (["doc", "docx", "txt", "rtf", "odt"].includes(ext)) {
      return "doc";
    }
    return "file";
  }

  const FILE_ICON_SVG = {
    image:
      '<svg viewBox="0 0 24 24" width="32" height="32" aria-hidden="true"><rect x="3" y="5" width="18" height="14" rx="2" fill="none" stroke="currentColor" stroke-width="1.8"/><circle cx="9" cy="10" r="1.6" fill="currentColor"/><path d="M3 16l5-4 4 3 3-2 6 4" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></svg>',
    video:
      '<svg viewBox="0 0 24 24" width="32" height="32" aria-hidden="true"><rect x="3" y="6" width="14" height="12" rx="2" fill="none" stroke="currentColor" stroke-width="1.8"/><path d="M17 10l4-2v8l-4-2z" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"/></svg>',
    audio:
      '<svg viewBox="0 0 24 24" width="32" height="32" aria-hidden="true"><path d="M9 18V6l10-2v12" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/><circle cx="7" cy="18" r="2.2" fill="none" stroke="currentColor" stroke-width="1.8"/><circle cx="17" cy="16" r="2.2" fill="none" stroke="currentColor" stroke-width="1.8"/></svg>',
    archive:
      '<svg viewBox="0 0 24 24" width="32" height="32" aria-hidden="true"><path d="M4 8V6a2 2 0 012-2h12a2 2 0 012 2v2M4 8h16v10a2 2 0 01-2 2H6a2 2 0 01-2-2V8z" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"/><path d="M12 8v10M10 12h4" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>',
    pdf:
      '<svg viewBox="0 0 24 24" width="32" height="32" aria-hidden="true"><path d="M7 3h7l5 5v13a1 1 0 01-1 1H7a1 1 0 01-1-1V4a1 1 0 011-1z" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"/><path d="M14 3v5h5M8.5 16h2.2a1.6 1.6 0 000-3.2H8.5V11H11M14 11v5h1.5a1.8 1.8 0 000-3.6H14" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/></svg>',
    app:
      '<svg viewBox="0 0 24 24" width="32" height="32" aria-hidden="true"><rect x="4" y="4" width="16" height="16" rx="3" fill="none" stroke="currentColor" stroke-width="1.8"/><path d="M8 8h.01M12 8h.01M16 8h.01M8 16h8" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>',
    code:
      '<svg viewBox="0 0 24 24" width="32" height="32" aria-hidden="true"><path d="M9 8l-4 4 4 4M15 8l4 4-4 4" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></svg>',
    doc:
      '<svg viewBox="0 0 24 24" width="32" height="32" aria-hidden="true"><path d="M7 3h7l5 5v13a1 1 0 01-1 1H7a1 1 0 01-1-1V4a1 1 0 011-1z" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"/><path d="M14 3v5h5M9 13h6M9 17h6" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></svg>',
    file:
      '<svg viewBox="0 0 24 24" width="32" height="32" aria-hidden="true"><path d="M7 3h7l5 5v13a1 1 0 01-1 1H7a1 1 0 01-1-1V4a1 1 0 011-1z" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"/><path d="M14 3v5h5" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"/></svg>',
  };

  const ACTION_SVG = {
    link: '<svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true"><path d="M10 13a5 5 0 007.07 0l1.41-1.41a5 5 0 00-7.07-7.07L10 5.93M14 11a5 5 0 00-7.07 0L5.52 12.4a5 5 0 007.07 7.07L14 18.07" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></svg>',
    folder:
      '<svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true"><path d="M3 8a2 2 0 012-2h4l2 2h8a2 2 0 012 2v8a2 2 0 01-2 2H5a2 2 0 01-2-2V8z" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"/></svg>',
    close:
      '<svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true"><path d="M6 6l12 12M18 6L6 18" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>',
  };

  function makeFileIcon(filename, mime) {
    const kind = fileKind(filename, mime);
    const wrap = document.createElement("span");
    wrap.className = `hist-file-icon is-${kind}`;
    wrap.setAttribute("aria-hidden", "true");
    wrap.innerHTML = FILE_ICON_SVG[kind] || FILE_ICON_SVG.file;
    return wrap;
  }

  function originFromUrl(url) {
    try {
      const parsed = new URL(url);
      if (parsed.protocol !== "http:" && parsed.protocol !== "https:") {
        return "";
      }
      return parsed.origin;
    } catch (_) {
      return "";
    }
  }

  function sourceInfo(entry) {
    const candidates = [
      entry && entry.originalUrl,
      entry && entry.url,
    ].filter((value) => typeof value === "string" && value.trim());

    for (const raw of candidates) {
      const value = raw.trim();
      if (value.startsWith("blob:")) {
        const inner = value.slice(5);
        const origin = originFromUrl(inner);
        if (origin) {
          return { display: origin, href: origin };
        }
        continue;
      }
      const origin = originFromUrl(value);
      if (origin) {
        return { display: origin, href: value };
      }
    }

    if (candidates[0]) {
      return { display: candidates[0], href: candidates[0] };
    }
    return { display: "", href: "" };
  }

  function siteIconCandidates(url, originalUrl) {
    const hosts = [];
    const pushHost = (value) => {
      try {
        let raw = String(value || "");
        if (raw.startsWith("blob:")) {
          raw = raw.slice(5);
        }
        const parsed = new URL(raw);
        if (parsed.protocol !== "http:" && parsed.protocol !== "https:") {
          return;
        }
        hosts.push(parsed.hostname, parsed.hostname.replace(/^www\./, ""));
      } catch (_) {
        /* ignore */
      }
    };
    pushHost(originalUrl);
    pushHost(url);
    const unique = Array.from(new Set(hosts.filter(Boolean)));
    const out = [];
    unique.forEach((host) => {
      const bare = host.replace(/^www\./, "");
      out.push(
        `https://icons.duckduckgo.com/ip3/${encodeURIComponent(host)}.ico`,
        `https://www.google.com/s2/favicons?domain=${encodeURIComponent(bare)}&sz=64`,
        `https://www.google.com/s2/favicons?domain=${encodeURIComponent(host)}&sz=64`,
        `https://logo.clearbit.com/${encodeURIComponent(bare)}`
      );
    });
    return out;
  }

  function makeDownloadIcon(entry) {
    const fallback = makeFileIcon(entry.filename, entry.mime);
    const wrap = document.createElement("span");
    wrap.className = "hist-site-icon";
    wrap.setAttribute("aria-hidden", "true");

    const candidates = siteIconCandidates(entry.url, entry.originalUrl);
    if (!candidates.length) {
      wrap.classList.add("is-fallback");
      wrap.append(fallback);
      return wrap;
    }

    const img = document.createElement("img");
    img.alt = "";
    img.draggable = false;
    img.decoding = "async";
    img.referrerPolicy = "no-referrer";
    let index = 0;
    const tryNext = () => {
      if (index >= candidates.length) {
        img.removeEventListener("error", tryNext);
        wrap.replaceChildren(fallback);
        wrap.classList.add("is-fallback");
        return;
      }
      img.src = candidates[index];
      index += 1;
    };
    img.addEventListener("error", tryNext);
    wrap.append(img);
    tryNext();
    return wrap;
  }

  function makeActionButton(label, svg, onClick) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "hist-tool";
    btn.setAttribute("aria-label", label);
    btn.title = label;
    btn.innerHTML = svg;
    btn.addEventListener("click", (event) => {
      event.preventDefault();
      event.stopPropagation();
      onClick();
    });
    return btn;
  }

  async function copyDownloadLink(url) {
    const value = String(url || "");
    if (!value) {
      return;
    }
    try {
      if (navigator.clipboard && navigator.clipboard.writeText) {
        await navigator.clipboard.writeText(value);
        return;
      }
    } catch (_) {
      /* fallback below */
    }
    const input = document.createElement("textarea");
    input.value = value;
    input.setAttribute("readonly", "");
    input.style.position = "fixed";
    input.style.left = "-9999px";
    document.body.append(input);
    input.select();
    try {
      document.execCommand("copy");
    } catch (_) {
      /* ignore */
    }
    input.remove();
  }

  function parseTs(ts) {
    const n = Number(ts);
    if (!Number.isFinite(n) || n <= 0) {
      return null;
    }
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

  function stateLabel(entry) {
    const state = String(entry.state || "");
    if (state === "in_progress") {
      const pct = Number(entry.percent);
      if (Number.isFinite(pct) && pct >= 0) {
        return `${pct}%`;
      }
      return "Downloading…";
    }
    if (state === "canceled") {
      return "Canceled";
    }
    if (state === "interrupted") {
      return "Interrupted";
    }
    return "";
  }

  function filteredEntries() {
    const q = query.trim().toLowerCase();
    if (!q) {
      return entries.slice();
    }
    return entries.filter((entry) => {
      const name = String(entry.filename || "").toLowerCase();
      const url = String(entry.url || "").toLowerCase();
      const path = String(entry.path || "").toLowerCase();
      return name.includes(q) || url.includes(q) || path.includes(q);
    });
  }

  function render() {
    const items = filteredEntries();
    listEl.replaceChildren();
    if (!items.length) {
      emptyEl.hidden = false;
      emptyEl.textContent = query.trim()
        ? "No matching downloads."
        : "No downloads yet.";
      return;
    }
    emptyEl.hidden = true;

    let lastDay = "";

    items.forEach((entry) => {
      const key = dayKey(entry.ts);
      if (key !== lastDay) {
        lastDay = key;
        const heading = document.createElement("h2");
        heading.className = "hist-day";
        heading.textContent = dayLabel(entry.ts);
        listEl.append(heading);
      }

      const card = document.createElement("article");
      card.className = "hist-file-box";

      const row = document.createElement("div");
      row.className = "hist-row";

      const icon = makeDownloadIcon(entry);

      const copy = document.createElement("div");
      copy.className = "hist-copy";
      const title = document.createElement("div");
      title.className = "hist-filename";
      title.textContent = entry.filename || entry.url || "download";

      const source = sourceInfo(entry);
      const status = stateLabel(entry);
      if (source.display) {
        const from = document.createElement("a");
        from.className = "hist-from";
        from.href = source.href || source.display;
        from.textContent = status
          ? `${status} · From ${source.display}`
          : `From ${source.display}`;
        from.addEventListener("click", (event) => {
          event.preventDefault();
          const target = source.href || source.display;
          callNative("browser.navigate", { url: target }).catch(() => {
            window.location.href = target;
          });
        });
        copy.append(title, from);
      } else {
        const meta = document.createElement("div");
        meta.className = "hist-domain";
        meta.textContent = status || "\u00a0";
        copy.append(title, meta);
      }

      const tools = document.createElement("div");
      tools.className = "hist-tools";
      tools.append(
        makeActionButton("Copy download link", ACTION_SVG.link, () => {
          const source = sourceInfo(entry);
          copyDownloadLink(source.href || entry.url || entry.originalUrl);
        }),
        makeActionButton("Show in folder", ACTION_SVG.folder, () => {
          if (!entry.path) {
            return;
          }
          callNative("downloads.showInFolder", { path: entry.path }).catch(
            () => {}
          );
        }),
        makeActionButton("Remove from downloads", ACTION_SVG.close, () => {
          removeIds([entry.id]);
        })
      );

      row.append(icon, copy, tools);
      card.append(row);
      listEl.append(card);
    });
  }

  async function loadEntries({ quiet = false } = {}) {
    try {
      const result = await callNative("downloads.list", {});
      entries = Array.isArray(result && result.entries) ? result.entries : [];
    } catch (_) {
      if (!quiet) {
        entries = [];
      }
    }
    render();
  }

  async function removeIds(ids) {
    const unique = Array.from(new Set(ids.filter(Boolean)));
    for (const id of unique) {
      try {
        await callNative("downloads.remove", { id });
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

  navOmni?.addEventListener("click", () => {
    navOmni.classList.add("is-active");
    navClear?.classList.remove("is-active");
  });

  navClear?.addEventListener("click", async () => {
    navClear.classList.add("is-active");
    navOmni?.classList.remove("is-active");
    const ok = window.confirm("Clear all downloads from this list?");
    if (!ok) {
      navOmni?.classList.add("is-active");
      navClear.classList.remove("is-active");
      return;
    }
    try {
      await callNative("downloads.clear", {});
    } catch (_) {
      /* ignore */
    }
    navOmni?.classList.add("is-active");
    navClear.classList.remove("is-active");
    await loadEntries();
  });

  loadEntries();
  window.setInterval(() => {
    if (entries.some((e) => e && e.state === "in_progress")) {
      loadEntries({ quiet: true });
    }
  }, 1000);
})();
