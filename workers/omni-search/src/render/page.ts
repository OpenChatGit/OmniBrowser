import { withCors } from "../cors";
import { omniBrandMarkImg } from "./brand";

/** Same start surface as Omni Browser — preview notice, clock, search. */
export function renderOmniHome(): Response {
  const html = `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <meta name="color-scheme" content="dark" />
  <title>QuBrain Search</title>
  <link rel="search" type="application/opensearchdescription+xml" title="QuBrain Search" href="/opensearch.xml" />
  <style>
    :root {
      --bg-deep: #121112;
      --text: #e4e4e4;
      --muted: #8a8a8a;
      --accent: #66c0f4;
      --font: "Segoe UI", system-ui, sans-serif;
      --font-brand: "Segoe UI Variable Display", "Segoe UI", system-ui, sans-serif;
    }
    * { box-sizing: border-box; }
    html, body {
      margin: 0;
      height: 100%;
      overflow: hidden;
      background: var(--bg-deep);
      color: var(--text);
      font-family: var(--font);
    }
    .browser-start {
      position: relative;
      min-height: 100%;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 2rem 1.25rem 3rem;
      overflow: hidden;
      background:
        radial-gradient(900px 520px at 50% 18%, rgba(102, 192, 244, 0.12) 0%, transparent 58%),
        radial-gradient(700px 420px at 80% 90%, rgba(92, 175, 45, 0.07) 0%, transparent 55%),
        linear-gradient(180deg, #161516 0%, #121112 55%, #0e0d0e 100%);
    }
    .browser-start-glow {
      position: absolute;
      inset: 18% 20% auto;
      height: 180px;
      border-radius: 50%;
      background: radial-gradient(circle, rgba(255, 255, 255, 0.06) 0%, transparent 70%);
      filter: blur(18px);
      pointer-events: none;
      animation: browser-glow 5.5s ease-in-out infinite alternate;
    }
    @keyframes browser-glow {
      from { opacity: 0.55; transform: scale(0.96); }
      to { opacity: 1; transform: scale(1.05); }
    }
    .browser-start-notice {
      position: absolute;
      top: 1.05rem;
      left: 50%;
      transform: translateX(-50%);
      z-index: 6;
      display: inline-flex;
      align-items: center;
      gap: 0.55rem;
      max-width: min(420px, calc(100% - 7.5rem));
      padding: 0.42rem 0.45rem 0.42rem 0.65rem;
      border-radius: 999px;
      border: 1px solid rgba(255, 255, 255, 0.12);
      background: rgba(36, 35, 36, 0.92);
      box-shadow: 0 10px 28px rgba(0, 0, 0, 0.35);
      backdrop-filter: blur(12px);
      color: #e8e8e8;
      font-size: 0.8rem;
      font-weight: 450;
      letter-spacing: -0.01em;
      line-height: 1.25;
      animation: browser-notice-in 520ms cubic-bezier(0.22, 1, 0.36, 1) both;
    }
    .browser-start-notice[hidden] { display: none !important; }
    .browser-start-notice-label {
      flex: 0 0 auto;
      padding: 0.12rem 0.45rem;
      border-radius: 999px;
      background: rgba(102, 192, 244, 0.18);
      color: #9fd8f8;
      font-size: 0.68rem;
      font-weight: 600;
      letter-spacing: 0.02em;
      text-transform: uppercase;
    }
    .browser-start-notice-text {
      min-width: 0;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    .browser-start-notice-close {
      appearance: none;
      flex: 0 0 auto;
      width: 26px;
      height: 26px;
      margin: 0;
      padding: 0;
      border: 0;
      border-radius: 999px;
      background: rgba(255, 255, 255, 0.06);
      color: #c8c8c8;
      font-size: 1.05rem;
      line-height: 1;
      cursor: pointer;
    }
    .browser-start-notice-close:hover {
      background: rgba(255, 255, 255, 0.12);
      color: #fff;
    }
    @keyframes browser-notice-in {
      from { opacity: 0; transform: translateX(-50%) translateY(-8px); }
      to { opacity: 1; transform: translateX(-50%) translateY(0); }
    }
    .browser-start-clock {
      position: absolute;
      top: 1rem;
      left: 1.15rem;
      z-index: 5;
      display: flex;
      align-items: flex-start;
      gap: 0.3rem;
      margin: 0;
      color: #ececec;
      user-select: none;
      pointer-events: none;
    }
    .browser-start-clock-time {
      font-size: 2.45rem;
      font-weight: 500;
      letter-spacing: -0.04em;
      line-height: 1;
      font-variant-numeric: tabular-nums;
    }
    .browser-start-clock-period {
      font-size: 0.88rem;
      font-weight: 500;
      letter-spacing: 0.01em;
      line-height: 1;
      margin-top: 0.2em;
      opacity: 0.92;
    }
    .browser-start-tools {
      position: absolute;
      top: 1rem;
      right: 1rem;
      z-index: 5;
    }
    .browser-start-link {
      display: inline-flex;
      align-items: center;
      height: 34px;
      padding: 0 0.85rem;
      border-radius: 8px;
      border: 1px solid rgba(255,255,255,0.1);
      background: rgba(36,35,36,0.75);
      color: var(--text);
      font-size: 0.82rem;
      font-weight: 500;
      text-decoration: none;
    }
    .browser-start-link:hover {
      background: rgba(255,255,255,0.08);
      text-decoration: none;
    }
    .browser-brand-row {
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 0.75rem;
      min-height: 44px;
      margin-bottom: 1.35rem;
      animation: browser-mark-in 520ms cubic-bezier(0.22, 1, 0.36, 1) both;
    }
    .browser-brand-mark {
      width: 40px;
      height: 40px;
      display: block;
      object-fit: contain;
      flex-shrink: 0;
      transform: translateY(1px);
    }
    .browser-brand {
      margin: 0;
      min-height: 40px;
      display: flex;
      align-items: center;
      font-family: var(--font-brand);
      font-size: clamp(1.85rem, 4.2vw, 2.85rem);
      font-weight: 600;
      letter-spacing: -0.045em;
      line-height: 1;
      color: var(--text);
      white-space: nowrap;
    }
    @keyframes browser-mark-in {
      from { opacity: 0; transform: translateY(10px); }
      to { opacity: 1; transform: translateY(0); }
    }
    .browser-search {
      position: relative;
      z-index: 1;
      width: min(560px, 100%);
      display: flex;
      align-items: center;
      gap: 0.55rem;
      height: 52px;
      min-height: 52px;
      padding: 0 0.55rem 0 1rem;
      border-radius: 999px;
      background: rgba(36, 35, 36, 0.88);
      border: 1px solid rgba(255, 255, 255, 0.1);
      box-shadow: 0 18px 48px rgba(0, 0, 0, 0.35);
      backdrop-filter: blur(12px);
      animation: browser-search-in 700ms cubic-bezier(0.22, 1, 0.36, 1) both;
    }
    @keyframes browser-search-in {
      from { opacity: 0; transform: translateY(14px) scale(0.98); }
      to { opacity: 1; transform: translateY(0) scale(1); }
    }
    .browser-search:focus-within {
      border-color: rgba(255, 255, 255, 0.2);
      box-shadow: 0 22px 56px rgba(0, 0, 0, 0.42);
    }
    .browser-search-icon {
      width: 18px;
      height: 18px;
      color: var(--muted);
      flex-shrink: 0;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      line-height: 0;
    }
    .browser-search-icon svg { display: block; width: 18px; height: 18px; }
    .browser-search-input {
      flex: 1 1 auto;
      min-width: 0;
      height: 18px;
      border: 0;
      outline: none;
      background: transparent;
      color: var(--text);
      font: inherit;
      font-size: 0.98rem;
      letter-spacing: -0.015em;
      line-height: 18px;
      padding: 0;
      margin: 0;
    }
    .browser-search-input::placeholder {
      color: #7a7a7a;
      opacity: 1;
      line-height: 18px;
    }
    .browser-search-go {
      appearance: none;
      border: 0;
      width: 34px;
      height: 34px;
      display: grid;
      place-items: center;
      border-radius: 999px;
      background: rgba(255, 255, 255, 0.1);
      color: var(--text);
      cursor: pointer;
      flex-shrink: 0;
    }
    .browser-search-go:hover { background: rgba(255, 255, 255, 0.16); }
    .browser-search-go svg { display: block; }
    @media (max-width: 640px) {
      .browser-start-clock { display: none; }
      .browser-start-notice { max-width: calc(100% - 6.5rem); left: 1rem; right: auto; transform: none; }
      .browser-start-notice-text { white-space: normal; }
    }
  </style>
</head>
<body>
  <div class="browser-start">
    <div class="browser-start-glow" aria-hidden="true"></div>
    <div class="browser-start-notice" id="browser-start-notice" role="status">
      <span class="browser-start-notice-label">Preview</span>
      <span class="browser-start-notice-text">UI is being worked on — not final.</span>
      <button type="button" class="browser-start-notice-close" id="browser-start-notice-close" aria-label="Dismiss">×</button>
    </div>
    <time class="browser-start-clock" id="browser-start-clock" aria-live="polite">
      <span class="browser-start-clock-time" id="browser-start-clock-time"></span>
      <span class="browser-start-clock-period" id="browser-start-clock-period"></span>
    </time>
    <div class="browser-start-tools">
      <a class="browser-start-link" href="/updates">Updates</a>
    </div>
    <div class="browser-brand-row">
      ${omniBrandMarkImg(40)}
      <h1 class="browser-brand">Omni Browser</h1>
    </div>
    <form class="browser-search" action="/search" method="get" role="search" autocomplete="off">
      <span class="browser-search-icon" aria-hidden="true">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.75" stroke-linecap="round" stroke-linejoin="round"><circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/></svg>
      </span>
      <input
        type="search"
        class="browser-search-input"
        name="q"
        spellcheck="false"
        autocomplete="off"
        aria-label="Search the web"
        placeholder="Search the web or type a URL"
        autofocus
      />
      <button type="submit" class="browser-search-go" aria-label="Go">
        <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12h14"/><path d="m12 5 7 7-7 7"/></svg>
      </button>
    </form>
  </div>
  <script>
    (function () {
      var KEY = "omni.ui.previewNoticeDismissed";
      var notice = document.getElementById("browser-start-notice");
      var closeBtn = document.getElementById("browser-start-notice-close");
      try {
        if (localStorage.getItem(KEY) === "1" && notice) notice.hidden = true;
      } catch (e) {}
      if (closeBtn && notice) {
        closeBtn.addEventListener("click", function () {
          notice.hidden = true;
          try { localStorage.setItem(KEY, "1"); } catch (e) {}
        });
      }
      var timeEl = document.getElementById("browser-start-clock-time");
      var periodEl = document.getElementById("browser-start-clock-period");
      function tick() {
        var now = new Date();
        var h = now.getHours();
        var m = now.getMinutes();
        var period = h >= 12 ? "PM" : "AM";
        var h12 = h % 12;
        if (h12 === 0) h12 = 12;
        if (timeEl) timeEl.textContent = h12 + ":" + String(m).padStart(2, "0");
        if (periodEl) periodEl.textContent = period;
      }
      tick();
      setInterval(tick, 15000);
    })();
  </script>
</body>
</html>`;

  return withCors(
    new Response(html, {
      headers: {
        "Content-Type": "text/html; charset=utf-8",
        "Cache-Control": "no-store",
      },
    }),
  );
}

export function openSearchXml(): Response {
  const xml = `<?xml version="1.0" encoding="UTF-8"?>
<OpenSearchDescription xmlns="http://a9.com/-/spec/opensearch/1.1/">
  <ShortName>QuBrain Search</ShortName>
  <Description>QuBrain web search</Description>
  <InputEncoding>UTF-8</InputEncoding>
  <Url type="text/html" method="get" template="https://search.qubrain.org/search?q={searchTerms}"/>
  <Url type="application/json" method="get" template="https://api.qubrain.org/v1/search?q={searchTerms}"/>
</OpenSearchDescription>`;
  return withCors(
    new Response(xml, {
      headers: {
        "Content-Type":
          "application/opensearchdescription+xml; charset=utf-8",
      },
    }),
  );
}
