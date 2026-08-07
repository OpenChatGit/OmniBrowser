import { withCors } from "../cors";
import type { AppUpdate } from "../updates";
import { omniBrandMarkImg } from "./brand";
import { formatUpdateDate, renderMarkdown } from "./markdown";

function shellCss(): string {
  return `
    :root {
      --bg-deep: #121112;
      --text: #e4e4e4;
      --muted: #8a8a8a;
      --accent: #66c0f4;
      --stroke: #2a292a;
      --font: "Segoe UI", system-ui, sans-serif;
      --font-brand: "Segoe UI Variable Display", "Segoe UI", system-ui, sans-serif;
    }
    * { box-sizing: border-box; }
    html, body {
      margin: 0;
      min-height: 100%;
      background:
        radial-gradient(1200px 600px at 50% -10%, rgba(102, 192, 244, 0.06), transparent 55%),
        linear-gradient(180deg, #161516 0%, var(--bg-deep) 48%, #101010 100%);
      color: var(--text);
      font-family: var(--font);
    }
    a { color: var(--accent); text-decoration: none; }
    a:hover { text-decoration: underline; }
    .hist {
      display: grid;
      grid-template-columns: 200px minmax(0, 1fr);
      align-items: start;
      gap: 24px;
      min-height: 100vh;
      padding: 28px 24px 48px;
    }
    .hist-aside {
      display: flex;
      flex-direction: column;
      align-items: flex-start;
      gap: 20px;
    }
    .hist-brand {
      display: inline-flex;
      align-items: center;
      gap: 10px;
      padding-left: 4px;
      text-decoration: none;
      color: var(--text);
    }
    .hist-brand:hover { text-decoration: none; }
    .hist-brand-mark { width: 28px; height: 28px; display: block; object-fit: contain; }
    .hist-brand-name {
      font-family: var(--font-brand);
      font-size: 1.45rem;
      font-weight: 600;
      letter-spacing: -0.03em;
    }
    .hist-navs { display: flex; flex-direction: column; gap: 4px; width: 100%; }
    .hist-nav {
      appearance: none;
      display: block;
      margin: 0;
      padding: 8px 12px;
      border: 1px solid transparent;
      border-radius: 8px;
      background: transparent;
      color: var(--muted);
      font: inherit;
      font-size: 0.88rem;
      font-weight: 500;
      text-align: left;
      text-decoration: none;
      cursor: pointer;
    }
    .hist-nav:hover { background: #2c2b2c; color: var(--text); text-decoration: none; }
    .hist-nav.is-active {
      border-color: var(--stroke);
      background: rgba(255,255,255,0.04);
      color: var(--text);
    }
    .hist-main {
      display: flex;
      flex-direction: column;
      align-items: center;
      min-width: 0;
      width: 100%;
      max-width: 820px;
      margin: 0 auto;
    }
    .info-section-title {
      margin: 8px 0 18px;
      width: 100%;
      font-family: var(--font-brand);
      font-size: 1.35rem;
      font-weight: 600;
      letter-spacing: -0.03em;
    }
    .info-updates { display: flex; flex-direction: column; gap: 14px; width: 100%; }
    .info-update {
      width: 100%;
      padding: 16px 16px 14px;
      border: 1px solid var(--stroke);
      border-radius: 10px;
      background: rgba(255,255,255,0.03);
    }
    .info-update-date {
      margin: 0 0 6px;
      font-size: 0.74rem;
      font-weight: 600;
      letter-spacing: 0.04em;
      text-transform: uppercase;
      color: var(--accent);
    }
    .info-update-title {
      margin: 0 0 10px;
      font-size: 1.05rem;
      font-weight: 600;
      letter-spacing: -0.02em;
    }
    .info-update-body {
      font-size: 0.9rem;
      font-weight: 450;
      line-height: 1.55;
      color: rgba(255,255,255,0.82);
    }
    .info-update-body > :first-child { margin-top: 0; }
    .info-update-body > :last-child { margin-bottom: 0; }
    .info-update-body p { margin: 0 0 0.75em; }
    .info-update-body h1, .info-update-body h2, .info-update-body h3 {
      margin: 0.9em 0 0.45em;
      font-family: var(--font-brand);
      font-weight: 600;
      letter-spacing: -0.02em;
      color: var(--text);
      line-height: 1.25;
    }
    .info-update-body h1 { font-size: 1.15rem; }
    .info-update-body h2 { font-size: 1.05rem; }
    .info-update-body h3 { font-size: 0.95rem; }
    .info-update-body ul, .info-update-body ol {
      margin: 0 0 0.75em;
      padding-left: 1.2em;
    }
    .info-update-body li { margin: 0.2em 0; }
    .info-update-body code {
      font-family: ui-monospace, Consolas, monospace;
      font-size: 0.86em;
      padding: 0.1em 0.35em;
      border-radius: 4px;
      background: rgba(255,255,255,0.06);
    }
    .info-update-body strong { font-weight: 600; color: var(--text); }
    .info-table-wrap {
      width: 100%;
      overflow-x: auto;
      margin: 0 0 0.9em;
      border: 1px solid var(--stroke);
      border-radius: 8px;
    }
    .info-update-body table {
      width: 100%;
      border-collapse: collapse;
      font-size: 0.84rem;
    }
    .info-update-body th, .info-update-body td {
      padding: 0.55rem 0.7rem;
      text-align: left;
      vertical-align: top;
      border-bottom: 1px solid rgba(255,255,255,0.06);
    }
    .info-update-body th {
      font-size: 0.72rem;
      font-weight: 600;
      letter-spacing: 0.03em;
      text-transform: uppercase;
      color: var(--muted);
      background: rgba(255,255,255,0.03);
    }
    .info-update-body tr:last-child td { border-bottom: 0; }
    .info-empty { color: var(--muted); margin: 0; width: 100%; }
    @media (max-width: 720px) {
      .hist { grid-template-columns: 1fr; padding: 20px 16px 40px; }
    }
  `;
}

export function renderUpdatesPage(updates: AppUpdate[]): Response {
  const cards =
    updates.length === 0
      ? `<p class="info-empty">No updates yet.</p>`
      : updates
          .map((entry) => {
            const title = entry.title
              ? `<h3 class="info-update-title">${escapeAttr(entry.title)}</h3>`
              : "";
            return `<article class="info-update">
              <p class="info-update-date">${escapeAttr(formatUpdateDate(entry.date))}</p>
              ${title}
              <div class="info-update-body">${renderMarkdown(entry.markdown)}</div>
            </article>`;
          })
          .join("");

  const html = `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <meta name="color-scheme" content="dark" />
  <title>Updates · QuBrain</title>
  <style>${shellCss()}</style>
</head>
<body>
  <div class="hist">
    <aside class="hist-aside" aria-label="Info">
      <a class="hist-brand" href="/">
        ${omniBrandMarkImg(28)}
        <span class="hist-brand-name">Info</span>
      </a>
      <nav class="hist-navs" aria-label="Info sections">
        <a class="hist-nav" href="/">Home</a>
        <a class="hist-nav is-active" href="/updates">Updates</a>
        <a class="hist-nav" href="/search">Search</a>
      </nav>
    </aside>
    <main class="hist-main">
      <h1 class="info-section-title">Updates</h1>
      <div class="info-updates">${cards}</div>
    </main>
  </div>
</body>
</html>`;

  return withCors(
    new Response(html, {
      headers: {
        "Content-Type": "text/html; charset=utf-8",
        "Cache-Control": "public, max-age=60",
      },
    }),
  );
}

function escapeAttr(value: string): string {
  return String(value || "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}
