import { withCors } from "../cors";
import { escapeHtml } from "../html";
import { lookupOfficial } from "../official";
import type { SearchInfobox, SearchResponse, SearchResult } from "../types";
import { omniBrandMarkImg } from "./brand";
import { renderOmniHome } from "./page";

function siteNameFromUrl(url: string, fallbackTitle: string): string {
  try {
    const host = new URL(url).hostname.replace(/^www\./, "");
    // Prefer readable brand for common multi-label hosts (en.wikipedia.org → Wikipedia).
    const labels = host.split(".").filter(Boolean);
    if (labels.length >= 2) {
      const brand = labels[labels.length - 2];
      if (brand.length > 2) {
        return brand.charAt(0).toUpperCase() + brand.slice(1);
      }
    }
    const base = labels[0] || host;
    if (base.length <= 1) {
      return fallbackTitle.split(/[|\-–—]/)[0]?.trim() || host;
    }
    return base.charAt(0).toUpperCase() + base.slice(1);
  } catch {
    return fallbackTitle.split(/[|\-–—]/)[0]?.trim() || url;
  }
}

function hostOf(url: string): string {
  try {
    return new URL(url).hostname.replace(/^www\./, "");
  } catch {
    return url;
  }
}

function displayUrl(url: string): string {
  try {
    const u = new URL(url);
    const host = u.hostname.replace(/^www\./, "");
    const path = u.pathname === "/" ? "" : u.pathname;
    return `${host}${path}`.replace(/\/$/, "") || host;
  } catch {
    return url;
  }
}

function faviconUrl(url: string, _size = 64): string {
  try {
    // Single CDN hop (DDG) — avoids Google s2 + /favicon.ico retry storms.
    const host = new URL(url).hostname.replace(/^www\./, "");
    return `https://icons.duckduckgo.com/ip3/${host}.ico`;
  } catch {
    return "";
  }
}

function thumbOf(result: SearchResult): string {
  return (
    result.thumbnail ||
    result.image ||
    result.thumbnails?.find(Boolean) ||
    ""
  );
}

function truncate(text: string, max = 220): string {
  const t = text.trim();
  if (t.length <= max) return t;
  return `${t.slice(0, max - 1).trimEnd()}…`;
}

function normalizeCat(raw: string | undefined): string {
  const c = String(raw || "general").toLowerCase();
  if (c === "all" || c === "web" || !c) return "general";
  if (c === "maps") return "map";
  return c;
}

function tabHref(q: string, cat: string, page = 1): string {
  const params = new URLSearchParams();
  params.set("q", q);
  if (cat && cat !== "general") params.set("tab", cat);
  if (page > 1) params.set("page", String(page));
  return `/search?${params.toString()}`;
}

function renderWebResult(result: SearchResult): string {
  const site = siteNameFromUrl(result.url, result.title);
  const urlLabel = displayUrl(result.url);
  const icon = faviconUrl(result.url);
  return `
    <a class="serp-result" href="${escapeHtml(result.url)}" rel="noopener noreferrer">
      <div class="serp-result-source">
        <span class="serp-result-favicon-wrap">
          ${
            icon
              ? `<img class="serp-result-favicon" src="${escapeHtml(icon)}" alt="" width="22" height="22" loading="lazy" />`
              : ""
          }
        </span>
        <div class="serp-result-site">
          <span class="serp-result-name">${escapeHtml(site)}</span>
          <span class="serp-result-url">${escapeHtml(urlLabel)}</span>
        </div>
      </div>
      <h2 class="serp-result-title">${escapeHtml(result.title)}</h2>
      <p class="serp-result-snippet">${escapeHtml(truncate(result.snippet))}</p>
    </a>`;
}

function renderImageResult(result: SearchResult): string {
  const src = thumbOf(result);
  if (!src) return "";
  const host = hostOf(result.url);
  const icon = faviconUrl(result.url, 32);
  return `
    <a class="serp-image is-ready" href="${escapeHtml(result.url)}" rel="noopener noreferrer" target="_blank">
      <img class="serp-image-thumb" src="${escapeHtml(src)}" alt="" loading="lazy" referrerpolicy="no-referrer" />
      <div class="serp-image-meta">
        ${
          icon
            ? `<img class="serp-image-favicon" src="${escapeHtml(icon)}" alt="" width="14" height="14" loading="lazy" />`
            : ""
        }
        <span class="serp-image-host">${escapeHtml(host)}</span>
      </div>
      <p class="serp-image-title">${escapeHtml(result.title)}</p>
    </a>`;
}

function renderVideoResult(result: SearchResult): string {
  const src = thumbOf(result);
  return `
    <a class="serp-video" href="${escapeHtml(result.url)}" rel="noopener noreferrer" target="_blank">
      ${
        src
          ? `<img class="serp-video-thumb" src="${escapeHtml(src)}" alt="" loading="lazy" referrerpolicy="no-referrer" />`
          : `<div class="serp-video-thumb" aria-hidden="true"></div>`
      }
      <div>
        <h2 class="serp-video-title">${escapeHtml(result.title)}</h2>
        <p class="serp-video-meta">${escapeHtml(result.source || hostOf(result.url))}</p>
        <p class="serp-video-snippet">${escapeHtml(truncate(result.snippet, 180))}</p>
      </div>
    </a>`;
}

function renderNewsResult(result: SearchResult): string {
  const icon = faviconUrl(result.url, 32);
  const src = thumbOf(result);
  const source = result.source || siteNameFromUrl(result.url, result.title);
  return `
    <a class="serp-news" href="${escapeHtml(result.url)}" rel="noopener noreferrer" target="_blank">
      <div class="serp-news-body">
        <div class="serp-news-meta">
          ${
            icon
              ? `<img class="serp-news-favicon" src="${escapeHtml(icon)}" alt="" width="22" height="22" loading="lazy" />`
              : ""
          }
          <span class="serp-news-source">${escapeHtml(source)}</span>
          ${
            result.published
              ? `<span class="serp-news-dot" aria-hidden="true">·</span><span class="serp-news-time">${escapeHtml(result.published)}</span>`
              : ""
          }
        </div>
        <h2 class="serp-news-title">${escapeHtml(result.title)}</h2>
        <p class="serp-news-snippet">${escapeHtml(truncate(result.snippet, 180))}</p>
      </div>
      ${
        src
          ? `<span class="serp-news-thumb-wrap is-ready"><img class="serp-news-thumb" src="${escapeHtml(src)}" alt="" loading="lazy" referrerpolicy="no-referrer" /></span>`
          : ""
      }
    </a>`;
}

function renderSidePanel(
  query: string,
  payload: SearchResponse,
): string {
  const cat = normalizeCat(payload.category);
  if (cat === "images" || cat === "news" || cat === "videos" || cat === "map") {
    return `<aside class="serp-side" id="serp-side" hidden></aside>`;
  }

  const ibox: SearchInfobox | undefined = payload.infobox;
  const official = lookupOfficial(query);
  const featured =
    payload.results.find((r) => r.featured) || payload.results[0];

  if (!ibox && !official && !featured) {
    return `<aside class="serp-side" id="serp-side" hidden></aside>`;
  }

  const title = ibox?.title || official?.title || featured?.title || query;
  const extract = ibox?.content || official?.snippet || featured?.snippet || "";
  const website = official?.url || ibox?.url || featured?.url || "";
  const image = ibox?.image || (website ? faviconUrl(website, 128) : "");
  const wikiUrl =
    ibox?.urls?.find((u) => /wikipedia\.org/i.test(u.url))?.url ||
    (ibox?.url && /wikipedia\.org/i.test(ibox.url) ? ibox.url : "") ||
    "";
  const facts = (ibox?.attributes || []).slice(0, 6);

  const factsHtml =
    facts.length === 0
      ? ""
      : `<div class="serp-side-box"><ul class="serp-side-facts">${facts
          .map(
            (f) =>
              `<li><span>${escapeHtml(f.label)}</span><strong>${escapeHtml(f.value)}</strong></li>`,
          )
          .join("")}</ul>${
          wikiUrl
            ? `<a class="serp-side-more" href="${escapeHtml(wikiUrl)}" rel="noopener noreferrer" target="_blank">More about ${escapeHtml(title)}</a>`
            : ""
        }</div>`;

  const moreSolo =
    !facts.length && wikiUrl
      ? `<a class="serp-side-more serp-side-more-solo" href="${escapeHtml(wikiUrl)}" rel="noopener noreferrer" target="_blank">More about ${escapeHtml(title)}</a>`
      : "";

  return `
    <aside class="serp-side" id="serp-side" aria-label="About ${escapeHtml(title)}">
      <div class="serp-side-card">
        <div class="serp-side-head">
          <div class="serp-side-head-text">
            <h2 class="serp-side-title">${escapeHtml(title)}</h2>
            ${
              website
                ? `<a class="serp-side-site" href="${escapeHtml(website)}" rel="noopener noreferrer" target="_blank">
                    <svg viewBox="0 0 24 24" width="14" height="14" aria-hidden="true"><circle cx="12" cy="12" r="9" fill="none" stroke="currentColor" stroke-width="1.8"/><path d="M3 12h18M12 3c3 3.5 3 14.5 0 18M12 3c-3 3.5-3 14.5 0 18" fill="none" stroke="currentColor" stroke-width="1.8"/></svg>
                    <span>${escapeHtml(hostOf(website))}</span>
                  </a>`
                : ""
            }
          </div>
          ${
            image
              ? `<img class="serp-side-mark" src="${escapeHtml(image)}" alt="" width="72" height="72" loading="lazy" referrerpolicy="no-referrer" />`
              : ""
          }
        </div>
        ${
          extract
            ? `<p class="serp-side-text">${escapeHtml(truncate(extract, 320))}${
                wikiUrl
                  ? ` <a class="serp-side-wiki-link" href="${escapeHtml(wikiUrl)}" rel="noopener noreferrer" target="_blank">Wikipedia</a>`
                  : ""
              }</p>`
            : ""
        }
        ${factsHtml}
        ${moreSolo}
      </div>
      <p class="serp-side-note">Data from search providers</p>
    </aside>`;
}

function renderResultsBody(payload: SearchResponse): string {
  const cat = normalizeCat(payload.category);
  const results = payload.results;

  if (results.length === 0) {
    return `<p class="serp-empty">${escapeHtml(payload.error || "No results found.")}</p>`;
  }

  if (cat === "images") {
    return `<div class="serp-images">${results.map(renderImageResult).join("")}</div>`;
  }
  if (cat === "videos") {
    return `<div class="serp-videos">${results.map(renderVideoResult).join("")}</div>`;
  }
  if (cat === "news") {
    return `<div class="serp-news-list">${results.map(renderNewsResult).join("")}</div>`;
  }
  return results.map(renderWebResult).join("");
}

function renderPager(payload: SearchResponse): string {
  const q = payload.query;
  const cat = normalizeCat(payload.category);
  if (cat === "images") return "";
  const page = Math.max(1, payload.page || 1);
  const hasMore = Boolean(payload.hasMore);
  if (page <= 1 && !hasMore) return "";

  const prev =
    page > 1
      ? `<a class="serp-prev" href="${escapeHtml(tabHref(q, cat, page - 1))}">Previous</a>`
      : "";
  const next = hasMore
    ? `<a class="serp-next" href="${escapeHtml(tabHref(q, cat, page + 1))}">Next page</a>`
    : "";

  return `<div class="serp-pager">${prev}${next}</div>`;
}

const SERP_CSS = `
*,*::before,*::after{box-sizing:border-box}
html,body{
  margin:0;min-height:100%;
  background:#1b1b1b;color:#e8e8e8;
  font-family:"Segoe UI",system-ui,-apple-system,sans-serif;
  -webkit-font-smoothing:antialiased;
}
a{color:#8ab4f8}
.serp{
  --serp-pad-x:1.35rem;--serp-brand-w:7.75rem;--serp-gap:1.35rem;
  --serp-content-left:calc(var(--serp-pad-x) + var(--serp-brand-w) + var(--serp-gap));
  --serp-results-w:40.5rem;min-height:100vh;
}
.serp-top{
  display:grid;
  grid-template-columns:var(--serp-brand-w) minmax(0,var(--serp-results-w));
  column-gap:var(--serp-gap);align-items:start;
  padding:1.15rem var(--serp-pad-x) 1rem;border-bottom:1px solid #2e2e2e;
}
.serp-brand{
  display:inline-flex;align-items:center;gap:.45rem;height:44px;
  color:#f2f2f2;text-decoration:none;user-select:none;
}
.serp-brand img,.serp-brand-mark{width:26px;height:26px;display:block;object-fit:contain}
.serp-brand-name{
  font-family:"Segoe UI Variable Display","Segoe UI",system-ui,sans-serif;
  font-size:1.2rem;font-weight:600;letter-spacing:-.045em;line-height:1;text-transform:lowercase;
}
.serp-top-main{min-width:0;width:100%}
.serp-search{
  display:flex;align-items:center;gap:.35rem;width:100%;min-height:44px;
  padding:0 .35rem 0 1.05rem;border-radius:999px;background:#2a2a2a;border:1px solid #3a3a3a;
}
.serp-search:focus-within{border-color:#5a5a5a}
.serp-search-input{
  flex:1 1 auto;min-width:0;border:0;outline:none;background:transparent;
  color:#f0f0f0;font:inherit;font-size:.98rem;letter-spacing:-.01em;
}
.serp-search-input::placeholder{color:#8d8d8d}
.serp-search-input::-webkit-search-cancel-button{-webkit-appearance:none}
.serp-search-go{
  appearance:none;border:0;width:38px;height:38px;display:inline-flex;
  align-items:center;justify-content:center;border-radius:999px;
  background:transparent;color:#cfcfcf;cursor:pointer;flex-shrink:0;padding:0;
}
.serp-search-go:hover{color:#fff;background:rgba(255,255,255,.06)}
.serp-tabs{
  display:flex;flex-wrap:wrap;align-items:center;gap:.15rem;
  margin:.55rem 0 0;padding:0 .15rem;
}
.serp-tab{
  appearance:none;border:0;background:transparent;color:#9a9a9a;
  font:inherit;font-size:.84rem;font-weight:500;letter-spacing:-.01em;
  padding:.4rem .7rem;border-radius:999px;cursor:pointer;line-height:1.25;
  text-decoration:none;
}
.serp-tab:hover{color:#e8e8e8;background:rgba(255,255,255,.06)}
.serp-tab.is-active{color:#f2f2f2;background:rgba(255,255,255,.1)}
.serp-page{padding:1.15rem var(--serp-pad-x) 3.5rem calc(var(--serp-content-left) + 25px)}
.serp-layout{
  display:grid;grid-template-columns:minmax(0,var(--serp-results-w)) minmax(240px,320px);
  column-gap:2.25rem;align-items:start;
}
.serp-main{
  min-width:0;width:100%;max-width:var(--serp-results-w);
  display:flex;flex-direction:column;gap:1.45rem;
}
.serp-main.is-media{max-width:none}
.serp-empty{margin:.5rem 0 0;color:#8d8d8d;font-size:.95rem}
.serp-pager{display:flex;justify-content:flex-start;align-items:center;gap:.65rem;margin:.75rem 0 1.5rem}
.serp-prev,.serp-next{
  appearance:none;border:1px solid rgba(255,255,255,.14);background:rgba(255,255,255,.06);
  color:#e8eaed;font:inherit;font-size:.92rem;font-weight:500;letter-spacing:-.01em;
  padding:.65rem 1.15rem;border-radius:10px;cursor:pointer;text-decoration:none;display:inline-flex;
}
.serp-prev:hover,.serp-next:hover{background:rgba(255,255,255,.1);border-color:rgba(255,255,255,.22)}
.serp-result{display:block;text-decoration:none;color:inherit;width:100%}
.serp-result:hover .serp-result-title{text-decoration:underline}
.serp-result-source{display:flex;align-items:center;gap:.65rem;margin:0 0 .55rem;min-width:0}
.serp-result-favicon-wrap{
  width:32px;height:32px;border-radius:8px;background:#fff;display:flex;
  align-items:center;justify-content:center;flex-shrink:0;overflow:hidden;padding:5px;box-sizing:border-box;
}
.serp-result-favicon{width:100%;height:100%;display:block;object-fit:contain}
.serp-result-site{display:flex;flex-direction:column;min-width:0;gap:.12rem}
.serp-result-name{color:#f1f1f1;font-size:.84rem;font-weight:600;letter-spacing:-.01em;line-height:1.15}
.serp-result-url{color:#9a9a9a;font-size:.74rem;letter-spacing:-.01em;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;line-height:1.2}
.serp-result-title{margin:0 0 .4rem;color:#8ab4f8;font-size:1.22rem;font-weight:500;letter-spacing:-.02em;line-height:1.25}
.serp-result-snippet{margin:0;color:#b8b8b8;font-size:.9rem;line-height:1.5;letter-spacing:-.01em}
.serp-side{position:sticky;top:1rem;padding:0;background:transparent;border:0}
.serp-side[hidden]{display:none}
.serp-side-card{padding:1.05rem 1.05rem 1.15rem;border-radius:14px;background:#242424;border:1px solid #333}
.serp-side-head{display:flex;align-items:flex-start;justify-content:space-between;gap:.85rem;margin-bottom:.75rem}
.serp-side-head-text{min-width:0;flex:1 1 auto}
.serp-side-title{margin:0;font-size:1.25rem;font-weight:650;letter-spacing:-.025em;line-height:1.15;color:#f3f3f3}
.serp-side-site{display:inline-flex;align-items:center;gap:.35rem;margin-top:.45rem;color:#8ab4f8;font-size:.8rem;text-decoration:none}
.serp-side-site:hover{text-decoration:underline}
.serp-side-mark{width:72px;height:72px;border-radius:12px;object-fit:cover;flex-shrink:0;background:#fff;padding:8px;box-sizing:border-box}
.serp-side-text{margin:0;color:#c0c0c0;font-size:.86rem;line-height:1.5}
.serp-side-wiki-link{color:#8ab4f8;text-decoration:none}
.serp-side-wiki-link:hover{text-decoration:underline}
.serp-side-box{margin-top:.9rem;padding:.15rem .85rem .75rem;border-radius:12px;background:#2c2c2c;border:1px solid #3a3a3a}
.serp-side-facts{margin:0;padding:0;list-style:none}
.serp-side-facts li{display:grid;grid-template-columns:minmax(4.5rem,38%) minmax(0,1fr);gap:.65rem;padding:.62rem 0;font-size:.8rem;line-height:1.35;border-bottom:1px solid #3a3a3a}
.serp-side-facts li:last-child{border-bottom:0}
.serp-side-facts span{color:#9a9a9a}
.serp-side-facts strong{color:#e8e8e8;font-weight:500}
.serp-side-more{display:flex;align-items:center;justify-content:center;gap:.35rem;margin-top:.15rem;padding:.55rem .4rem .2rem;color:#cfcfcf;font-size:.82rem;font-weight:500;text-decoration:none;border-top:1px solid #3a3a3a}
.serp-side-more:hover{color:#fff}
.serp-side-more-solo{margin-top:.85rem;border:1px solid #3a3a3a;border-radius:10px;padding:.6rem .75rem;background:#2c2c2c}
.serp-side-note{margin:.55rem 0 0 .15rem;color:#6a6a6a;font-size:.72rem;letter-spacing:.01em}
.serp-images{display:flex;flex-wrap:wrap;gap:.65rem .55rem;width:100%;align-items:flex-start}
.serp-image{display:block;width:calc(25% - .45rem);min-width:140px;max-width:220px;text-decoration:none;color:inherit}
.serp-image-thumb{display:block;height:160px;width:100%;border-radius:6px;object-fit:cover;background:#242424}
.serp-image-meta{display:flex;align-items:center;gap:.35rem;margin:.35rem 0 .08rem;min-width:0;overflow:hidden}
.serp-image-favicon{width:14px;height:14px;border-radius:3px;flex-shrink:0;object-fit:contain}
.serp-image-host{color:#9a9a9a;font-size:.72rem;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;min-width:0}
.serp-image-title{margin:0;color:#e8e8e8;font-size:.78rem;line-height:1.35;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.serp-videos{display:flex;flex-direction:column;gap:1rem;width:100%}
.serp-video{display:grid;grid-template-columns:168px minmax(0,1fr);gap:.85rem;text-decoration:none;color:inherit;align-items:start}
.serp-video-thumb{width:168px;height:94px;object-fit:cover;border-radius:10px;background:#2a2a2a;display:block}
.serp-video-title{margin:0 0 .35rem;color:#8ab4f8;font-size:1.05rem;font-weight:500;line-height:1.3}
.serp-video:hover .serp-video-title{text-decoration:underline}
.serp-video-meta{margin:0 0 .35rem;color:#9a9a9a;font-size:.78rem}
.serp-video-snippet{margin:0;color:#b5b5b5;font-size:.86rem;line-height:1.45}
.serp-news-list{display:flex;flex-direction:column;gap:1.35rem;width:100%}
.serp-news{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:1rem 1.15rem;align-items:start;text-decoration:none;color:inherit;width:100%}
.serp-news-body{min-width:0}
.serp-news-meta{display:flex;align-items:center;gap:.4rem;margin:0 0 .4rem;min-width:0}
.serp-news-favicon{width:22px;height:22px;border-radius:5px;flex-shrink:0;object-fit:contain;background:#2a2a2a}
.serp-news-source,.serp-news-time{color:#9a9a9a;font-size:.78rem;white-space:nowrap}
.serp-news-source{overflow:hidden;text-overflow:ellipsis;min-width:0}
.serp-news-dot{color:#6a6a6a;flex-shrink:0}
.serp-news-title{margin:0 0 .35rem;color:#8ab4f8;font-size:1.12rem;font-weight:500;letter-spacing:-.02em;line-height:1.3}
.serp-news:hover .serp-news-title{text-decoration:underline}
.serp-news-snippet{margin:0;color:#b5b5b5;font-size:.88rem;line-height:1.45;display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical;overflow:hidden}
.serp-news-thumb-wrap{width:120px;height:120px;flex-shrink:0;border-radius:16px;overflow:hidden;background:#242424}
.serp-news-thumb{display:block;width:100%;height:100%;object-fit:cover}
.serp.is-images .serp-layout{grid-template-columns:minmax(0,1fr)}
.serp.is-images .serp-main{max-width:none;width:100%}
.serp.is-images .serp-side{display:none}
.serp.is-images .serp-page{padding-left:var(--serp-pad-x);padding-right:var(--serp-pad-x)}
.serp.is-news .serp-side{display:none}
.serp.is-news .serp-layout{grid-template-columns:minmax(0,min(42rem,100%))}
.serp.is-videos .serp-side{display:none}
@media (max-width:920px){
  .serp{--serp-brand-w:6.5rem;--serp-gap:1rem}
  .serp-layout{grid-template-columns:1fr}
  .serp-side{position:static}
  .serp-page{padding-left:var(--serp-pad-x);padding-right:var(--serp-pad-x)}
  .serp-top{grid-template-columns:1fr}
  .serp-brand{margin-bottom:.35rem}
}
@media (max-width:640px){
  .serp-image{width:calc(50% - .3rem);min-width:0;max-width:none}
  .serp-video{grid-template-columns:120px minmax(0,1fr)}
  .serp-video-thumb{width:120px;height:68px}
  .serp-news-thumb-wrap{width:88px;height:88px;border-radius:12px}
}
`;

export function renderSearchPage(payload: SearchResponse): Response {
  const q = payload.query;
  if (!q.trim()) {
    return renderOmniHome();
  }

  const cat = normalizeCat(payload.category);
  const rootClass =
    cat === "images"
      ? "serp is-images"
      : cat === "news"
        ? "serp is-news"
        : cat === "videos"
          ? "serp is-videos"
          : "serp";
  const mainClass =
    cat === "images" || cat === "videos" || cat === "news"
      ? "serp-main is-media"
      : "serp-main";

  const tabs = [
    { id: "general", label: "All" },
    { id: "images", label: "Images" },
    { id: "news", label: "News" },
    { id: "videos", label: "Videos" },
  ]
    .map((tab) => {
      const active = cat === tab.id || (tab.id === "general" && cat === "general");
      return `<a class="serp-tab${active ? " is-active" : ""}" href="${escapeHtml(tabHref(q, tab.id))}">${tab.label}</a>`;
    })
    .join("");

  const html = `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <meta name="color-scheme" content="dark" />
  <title>${escapeHtml(q)} · QuBrain Search</title>
  <link rel="search" type="application/opensearchdescription+xml" title="QuBrain Search" href="/opensearch.xml" />
  <style>${SERP_CSS}</style>
</head>
<body>
  <div class="${rootClass}">
    <header class="serp-top">
      <a class="serp-brand" href="/" aria-label="QuBrain home">
        ${omniBrandMarkImg(26)}
        <span class="serp-brand-name">QuBrain</span>
      </a>
      <div class="serp-top-main">
        <form class="serp-search" action="/search" method="get" autocomplete="off" role="search">
          <input
            class="serp-search-input"
            type="search"
            name="q"
            value="${escapeHtml(q)}"
            placeholder="Search the web"
            spellcheck="false"
            aria-label="Search"
          />
          ${cat !== "general" ? `<input type="hidden" name="tab" value="${escapeHtml(cat)}" />` : ""}
          <button type="submit" class="serp-search-go" aria-label="Search">
            <svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true">
              <circle cx="11" cy="11" r="7" fill="none" stroke="currentColor" stroke-width="2" />
              <path d="M20 20l-3.5-3.5" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" />
            </svg>
          </button>
        </form>
        <nav class="serp-tabs" aria-label="Search categories">${tabs}</nav>
      </div>
    </header>
    <div class="serp-page">
      <div class="serp-layout">
        <main class="${mainClass}" id="serp-results" aria-live="polite">
          ${renderResultsBody(payload)}
          ${renderPager(payload)}
        </main>
        ${renderSidePanel(q, payload)}
      </div>
    </div>
  </div>
</body>
</html>`;

  const ok = payload.results.length > 0 && !payload.error;
  return withCors(
    new Response(html, {
      headers: {
        "Content-Type": "text/html; charset=utf-8",
        // Short edge/browser cache for successful SERPs; errors stay fresh.
        "Cache-Control": ok
          ? "public, max-age=60, stale-while-revalidate=300"
          : "no-store",
      },
    }),
  );
}
