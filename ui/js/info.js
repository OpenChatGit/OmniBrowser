(() => {
  const navs = Array.from(document.querySelectorAll(".hist-nav[data-nav]"));
  const panels = Array.from(document.querySelectorAll(".info-panel[data-panel]"));
  const updatesEl = document.getElementById("info-updates");
  const statusEl = document.getElementById("info-updates-status");

  let updatesLoaded = false;
  let updatesLoading = false;

  function apiBase() {
    try {
      const override = localStorage.getItem("qubrain.searchApi");
      if (override && /^https?:\/\//i.test(override)) {
        return override.replace(/\/+$/, "");
      }
    } catch (_) {
      // ignore
    }
    return "https://api.qubrain.org";
  }

  function escapeHtml(value) {
    return String(value || "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function formatDate(iso) {
    const raw = String(iso || "").trim();
    const match = raw.match(/^(\d{4})-(\d{2})-(\d{2})/);
    if (!match) {
      return raw || "Update";
    }
    const date = new Date(`${match[1]}-${match[2]}-${match[3]}T12:00:00Z`);
    if (Number.isNaN(date.getTime())) {
      return raw;
    }
    try {
      return new Intl.DateTimeFormat(undefined, {
        year: "numeric",
        month: "long",
        day: "numeric",
      }).format(date);
    } catch (_) {
      return raw;
    }
  }

  function inlineMarkdown(text) {
    let html = escapeHtml(text);
    html = html.replace(
      /\[([^\]]+)\]\((https?:\/\/[^)\s]+)\)/g,
      '<a href="$2" target="_blank" rel="noopener noreferrer">$1</a>'
    );
    html = html.replace(/`([^`]+)`/g, "<code>$1</code>");
    html = html.replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>");
    html = html.replace(/(^|[^*])\*([^*]+)\*(?!\*)/g, "$1<em>$2</em>");
    return html;
  }

  function renderMarkdown(source) {
    const lines = String(source || "").replace(/\r\n/g, "\n").split("\n");
    const parts = [];
    let i = 0;

    function flushParagraph(buf) {
      const text = buf.join(" ").trim();
      if (text) {
        parts.push(`<p>${inlineMarkdown(text)}</p>`);
      }
      buf.length = 0;
    }

    while (i < lines.length) {
      const line = lines[i];
      const trimmed = line.trim();

      if (!trimmed) {
        i += 1;
        continue;
      }

      const heading = trimmed.match(/^(#{1,3})\s+(.+)$/);
      if (heading) {
        const level = heading[1].length;
        parts.push(
          `<h${level}>${inlineMarkdown(heading[2].trim())}</h${level}>`
        );
        i += 1;
        continue;
      }

      if (/^[-*]\s+/.test(trimmed)) {
        const items = [];
        while (i < lines.length && /^[-*]\s+/.test(lines[i].trim())) {
          items.push(
            `<li>${inlineMarkdown(lines[i].trim().replace(/^[-*]\s+/, ""))}</li>`
          );
          i += 1;
        }
        parts.push(`<ul>${items.join("")}</ul>`);
        continue;
      }

      if (/^\d+\.\s+/.test(trimmed)) {
        const items = [];
        while (i < lines.length && /^\d+\.\s+/.test(lines[i].trim())) {
          items.push(
            `<li>${inlineMarkdown(lines[i].trim().replace(/^\d+\.\s+/, ""))}</li>`
          );
          i += 1;
        }
        parts.push(`<ol>${items.join("")}</ol>`);
        continue;
      }

      // GFM table: header | --- | rows
      if (
        trimmed.includes("|") &&
        i + 1 < lines.length &&
        /^\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)+\|?$/.test(lines[i + 1].trim())
      ) {
        function splitRow(row) {
          return row
            .replace(/^\|/, "")
            .replace(/\|$/, "")
            .split("|")
            .map((cell) => cell.trim());
        }
        const header = splitRow(trimmed);
        i += 2; // skip header + separator
        const bodyRows = [];
        while (i < lines.length && lines[i].trim().includes("|")) {
          bodyRows.push(splitRow(lines[i].trim()));
          i += 1;
        }
        const thead = `<thead><tr>${header
          .map((cell) => `<th>${inlineMarkdown(cell)}</th>`)
          .join("")}</tr></thead>`;
        const tbody = `<tbody>${bodyRows
          .map(
            (row) =>
              `<tr>${row
                .map((cell) => `<td>${inlineMarkdown(cell)}</td>`)
                .join("")}</tr>`
          )
          .join("")}</tbody>`;
        parts.push(`<div class="info-table-wrap"><table>${thead}${tbody}</table></div>`);
        continue;
      }

      const para = [];
      while (i < lines.length) {
        const next = lines[i].trim();
        if (
          !next ||
          /^#{1,3}\s+/.test(next) ||
          /^[-*]\s+/.test(next) ||
          /^\d+\.\s+/.test(next) ||
          (next.includes("|") &&
            i + 1 < lines.length &&
            /^\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)+\|?$/.test(lines[i + 1].trim()))
        ) {
          break;
        }
        para.push(next);
        i += 1;
      }
      flushParagraph(para);
    }

    return parts.join("");
  }

  function setStatus(text, show) {
    if (!statusEl) {
      return;
    }
    statusEl.textContent = text || "";
    statusEl.hidden = !show;
  }

  function renderUpdates(list) {
    if (!updatesEl) {
      return;
    }
    updatesEl.replaceChildren();
    if (!list.length) {
      setStatus("No updates yet.", true);
      return;
    }
    setStatus("", false);
    list.forEach((entry) => {
      const card = document.createElement("article");
      card.className = "info-update";

      const date = document.createElement("p");
      date.className = "info-update-date";
      date.textContent = formatDate(entry.date);
      card.append(date);

      if (entry.title) {
        const title = document.createElement("h3");
        title.className = "info-update-title";
        title.textContent = entry.title;
        card.append(title);
      }

      const body = document.createElement("div");
      body.className = "info-update-body";
      body.innerHTML = renderMarkdown(entry.markdown || "");
      card.append(body);

      updatesEl.append(card);
    });
  }

  async function loadUpdates() {
    if (!updatesEl || updatesLoaded || updatesLoading) {
      return;
    }
    updatesLoading = true;
    setStatus("Loading updates…", true);
    try {
      const response = await fetch(`${apiBase()}/v1/updates`, {
        headers: { Accept: "application/json" },
      });
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }
      const data = await response.json();
      const list = Array.isArray(data.updates) ? data.updates : [];
      updatesLoaded = true;
      renderUpdates(list);
    } catch (_) {
      setStatus("Could not load updates from Cloudflare.", true);
      if (updatesEl) {
        updatesEl.replaceChildren();
      }
    } finally {
      updatesLoading = false;
    }
  }

  function show(navId) {
    const id = String(navId || "about");
    navs.forEach((btn) => {
      btn.classList.toggle("is-active", btn.getAttribute("data-nav") === id);
    });
    panels.forEach((panel) => {
      const match = panel.getAttribute("data-panel") === id;
      panel.hidden = !match;
      panel.classList.toggle("is-active", match);
    });
    if (id === "updates") {
      loadUpdates();
    }
  }

  navs.forEach((btn) => {
    btn.addEventListener("click", () => {
      show(btn.getAttribute("data-nav"));
    });
  });

  show("about");
})();
