import type {
  BrowserBinding,
  Env,
  SearchResponse,
  SearchResult,
  WorkerExecutionContext,
} from "../types";
import { attr, stripTags, unwrapRedirectUrl } from "../html";
import { polishResults } from "../rank";
import { readSearchCache, writeSearchCache } from "../cache";
import {
  ingestSearchHits,
  searchOwnIndex,
  searchVectorIndex,
  withTimeout,
} from "./own-index";
import { searchSearxng } from "./searxng";
import { searchPlacesDense } from "../geocode";

function pushUnique(
  results: SearchResult[],
  seen: Set<string>,
  item: SearchResult,
  limit: number,
): void {
  if (results.length >= limit) {
    return;
  }
  const url = unwrapRedirectUrl(item.url);
  const title = stripTags(item.title);
  if (!url || !title || seen.has(url)) {
    return;
  }
  if (!/^https?:\/\//i.test(url)) {
    return;
  }
  try {
    const host = new URL(url).hostname;
    if (
      host === "duckduckgo.com" ||
      host.endsWith(".duckduckgo.com") ||
      host === "bing.com" ||
      host.endsWith(".bing.com")
    ) {
      return;
    }
  } catch {
    return;
  }
  seen.add(url);
  results.push({
    title,
    url,
    snippet: stripTags(item.snippet || ""),
  });
}

function parseBingHtml(
  html: string,
  query: string,
  limit: number,
): SearchResponse {
  const seen = new Set<string>();
  const results: SearchResult[] = [];

  const blockRe =
    /<li[^>]*class="[^"]*\bb_algo\b[^"]*"[^>]*>([\s\S]*?)<\/li>/gi;
  let match: RegExpExecArray | null;
  while ((match = blockRe.exec(html)) !== null && results.length < limit) {
    const block = match[1];
    const link = block.match(/<h2[^>]*>\s*<a\b([^>]*)>([\s\S]*?)<\/a>/i);
    if (!link) {
      continue;
    }
    const snip =
      block.match(
        /<(?:p|div)[^>]*class="[^"]*(?:b_lineclamp|b_caption)[^"]*"[^>]*>([\s\S]*?)<\/(?:p|div)>/i,
      ) ||
      block.match(/class="b_caption"[^>]*>[\s\S]*?<p[^>]*>([\s\S]*?)<\/p>/i);
    pushUnique(
      results,
      seen,
      {
        title: link[2],
        url: attr(link[1], "href"),
        snippet: snip?.[1] || "",
      },
      limit,
    );
  }

  return {
    query,
    results,
    provider: "bing",
    error: results.length === 0 ? "Bing returned no matches" : undefined,
  };
}

function parseDuckHtml(
  html: string,
  query: string,
  limit: number,
): SearchResponse {
  if (
    /anomaly-modal|Unfortunately, bots use DuckDuckGo too|challenge-form|captcha/i.test(
      html,
    )
  ) {
    return {
      query,
      results: [],
      provider: "duckduckgo",
      error: "DuckDuckGo blocked automated requests",
    };
  }

  const seen = new Set<string>();
  const results: SearchResult[] = [];

  // Prefer whole result cards so title + snippet stay paired.
  const cardRe =
    /<(?:div|article)[^>]*class="[^"]*\bresult\b[^"]*"[^>]*>([\s\S]*?)(?=<(?:div|article)[^>]*class="[^"]*\bresult\b|<\/(?:body|main|ol|ul)>)/gi;
  let card: RegExpExecArray | null;
  let foundCards = false;
  while ((card = cardRe.exec(html)) !== null && results.length < limit) {
    foundCards = true;
    const block = card[1];
    const link = block.match(
      /<a\b([^>]*\bresult__a\b[^>]*)>([\s\S]*?)<\/a>/i,
    );
    if (!link) {
      continue;
    }
    const snip = block.match(
      /<(?:a|td|div|span)\b[^>]*\bresult__snippet\b[^>]*>([\s\S]*?)<\/(?:a|td|div|span)>/i,
    );
    pushUnique(
      results,
      seen,
      {
        title: link[2],
        url: attr(link[1], "href"),
        snippet: snip?.[1] || "",
      },
      limit,
    );
  }

  if (!foundCards || results.length === 0) {
    // Fallback: title links, then nearest following snippet.
    const linkRe = /<a\b([^>]*\bresult__a\b[^>]*)>([\s\S]*?)<\/a>/gi;
    let link: RegExpExecArray | null;
    while ((link = linkRe.exec(html)) !== null && results.length < limit) {
      const after = html.slice(link.index, link.index + 2500);
      const snip = after.match(
        /<(?:a|td|div|span)\b[^>]*\bresult__snippet\b[^>]*>([\s\S]*?)<\/(?:a|td|div|span)>/i,
      );
      pushUnique(
        results,
        seen,
        {
          title: link[2],
          url: attr(link[1], "href"),
          snippet: snip?.[1] || "",
        },
        limit,
      );
    }
  }

  return {
    query,
    results,
    provider: "duckduckgo",
    error: results.length === 0 ? "DuckDuckGo returned no matches" : undefined,
  };
}

function parseGoogleHtml(
  html: string,
  query: string,
  limit: number,
): SearchResponse {
  if (
    /unusual traffic|detected unusual|captcha-form|id="captcha"|Before you continue to Google/i.test(
      html,
    ) &&
    !/<h3[\s>]/i.test(html) &&
    !/class="r"|class='r'/i.test(html)
  ) {
    return {
      query,
      results: [],
      provider: "google",
      error: "Google blocked or showed a consent/captcha page",
    };
  }

  const seen = new Set<string>();
  const results: SearchResult[] = [];

  const pushHref = (hrefRaw: string, title: string, snippet: string) => {
    let href = hrefRaw;
    if (href.startsWith("/url?") || href.startsWith("/url?")) {
      href = `https://www.google.com${href}`;
    } else if (href.startsWith("/")) {
      href = `https://www.google.com${href}`;
    }
    if (/google\.[^/]+\/(search|aclk|imgres|maps)/i.test(href)) {
      return;
    }
    pushUnique(
      results,
      seen,
      { title, url: href, snippet },
      limit,
    );
  };

  // Modern SERP: anchor wrapping an h3
  const titleRe =
    /<a\b([^>]*href="([^"]+)"[^>]*)>\s*(?:<div[^>]*>\s*)?<h3\b[^>]*>([\s\S]*?)<\/h3>/gi;
  let match: RegExpExecArray | null;
  while ((match = titleRe.exec(html)) !== null && results.length < limit) {
    const after = html.slice(match.index, match.index + 3500);
    const snip =
      after.match(
        /<(?:div|span)[^>]*(?:class="[^"]*(?:VwiC3b|yXK7lf|IT8tAd|kb0tQb|st)[^"]*"|data-sncf)[^>]*>([\s\S]*?)<\/(?:div|span)>/i,
      ) ||
      after.match(
        /<div[^>]*style="[^"]*line-clamp[^"]*"[^>]*>([\s\S]*?)<\/div>/i,
      );
    pushHref(match[2], match[3], snip?.[1] || "");
  }

  // Classic / gbv=1: <h3 class="r"><a href="...">title</a></h3> ... <span class="st">
  if (results.length < Math.min(3, limit)) {
    const classicRe =
      /<h3[^>]*class="[^"]*\br\b[^"]*"[^>]*>\s*<a\b([^>]*href="([^"]+)"[^>]*)>([\s\S]*?)<\/a>\s*<\/h3>([\s\S]{0,1200}?)(?=<h3|<\/div>)/gi;
    let classic: RegExpExecArray | null;
    while ((classic = classicRe.exec(html)) !== null && results.length < limit) {
      const snip = classic[4].match(
        /<(?:span|div)[^>]*class="[^"]*\bst\b[^"]*"[^>]*>([\s\S]*?)<\/(?:span|div)>/i,
      );
      pushHref(classic[2], classic[3], snip?.[1] || "");
    }
  }

  // Very loose fallback: any /url?q= http link with visible text
  if (results.length === 0) {
    const looseRe =
      /<a\b[^>]*href="(\/url\?[^"]+|https?:\/\/(?!www\.google\.)[^"]+)"[^>]*>([\s\S]*?)<\/a>/gi;
    let loose: RegExpExecArray | null;
    while ((loose = looseRe.exec(html)) !== null && results.length < limit) {
      const title = stripTags(loose[2]);
      if (title.length < 3 || title.length > 200) {
        continue;
      }
      if (/^(images|videos|maps|news|shopping|books)$/i.test(title)) {
        continue;
      }
      pushHref(loose[1], title, "");
    }
  }

  return {
    query,
    results,
    provider: "google",
    error: results.length === 0 ? "Google returned no matches" : undefined,
  };
}

async function browserFetchHtml(
  browser: BrowserBinding,
  url: string,
  options?: {
    waitUntil?: string;
    timeout?: number;
    waitForSelector?: string;
    cookies?: Array<Record<string, string>>;
  },
): Promise<{ ok: true; html: string } | { ok: false; error: string }> {
  const payload: Record<string, unknown> = {
    url,
    userAgent:
      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36",
    gotoOptions: {
      waitUntil: options?.waitUntil || "domcontentloaded",
      timeout: options?.timeout || 28000,
    },
  };
  if (options?.cookies?.length) {
    payload.cookies = options.cookies;
  }
  if (options?.waitForSelector) {
    const pageTimeout = Number(options?.timeout || 28000);
    payload.waitForSelector = {
      selector: options.waitForSelector,
      timeout: Math.min(8000, Math.max(2000, pageTimeout - 2000)),
    };
  }

  const response = await browser.quickAction("content", payload);

  if (!response.ok) {
    const detail = (await response.text()).slice(0, 300);
    return {
      ok: false,
      error: `Browser Rendering failed (${response.status}): ${detail}`,
    };
  }

  const data = (await response.json()) as {
    success?: boolean;
    result?: string | { html?: string; content?: string };
    errors?: Array<{ message?: string }>;
  };

  if (!data.success) {
    return {
      ok: false,
      error:
        data.errors?.map((e) => e.message).filter(Boolean).join("; ") ||
        "Browser Rendering returned unsuccessful response",
    };
  }

  const html =
    typeof data.result === "string"
      ? data.result
      : data.result?.html || data.result?.content || "";

  if (!html) {
    return { ok: false, error: "Browser Rendering returned empty HTML" };
  }

  return { ok: true, html };
}

/**
 * Browser Rendering fallback — Bing + DDG in parallel (time-boxed),
 * Google only if both return empty. Avoids 25s+ sequential chains.
 */
async function browserWebSearch(
  query: string,
  limit: number,
  browser: BrowserBinding,
): Promise<SearchResponse> {
  const errors: string[] = [];

  const bingUrl = `https://www.bing.com/search?q=${encodeURIComponent(query)}&setlang=en-US&cc=US`;
  const ddgUrl = `https://html.duckduckgo.com/html/?q=${encodeURIComponent(query)}`;

  const [bingPage, ddgPage] = await Promise.all([
    browserFetchHtml(browser, bingUrl, {
      waitUntil: "domcontentloaded",
      timeout: 12000,
      waitForSelector: "li.b_algo, h2",
    }),
    browserFetchHtml(browser, ddgUrl, {
      waitUntil: "domcontentloaded",
      timeout: 10000,
    }),
  ]);

  if (bingPage.ok) {
    const parsed = parseBingHtml(bingPage.html, query, limit);
    if (parsed.results.length > 0) {
      return { ...parsed, provider: "qsearch" };
    }
    errors.push(parsed.error || "Bing returned no matches");
  } else {
    errors.push(bingPage.error);
  }

  if (ddgPage.ok) {
    const parsed = parseDuckHtml(ddgPage.html, query, limit);
    if (parsed.results.length > 0) {
      return { ...parsed, provider: "qsearch" };
    }
    errors.push(parsed.error || "DuckDuckGo returned no matches");
  } else {
    errors.push(ddgPage.error);
  }

  const googleUrl =
    `https://www.google.com/search?q=${encodeURIComponent(query)}` +
    `&hl=en&gl=us&num=${Math.min(limit, 10)}&pws=0&gbv=1`;
  const googlePage = await browserFetchHtml(browser, googleUrl, {
    waitUntil: "domcontentloaded",
    timeout: 8000,
    cookies: [
      {
        name: "CONSENT",
        value: "YES+cb.20240401-00-p0.en+FX+999",
        domain: ".google.com",
        path: "/",
      },
    ],
  });
  if (googlePage.ok) {
    const parsed = parseGoogleHtml(googlePage.html, query, limit);
    if (parsed.results.length > 0) {
      return { ...parsed, provider: "qsearch" };
    }
    errors.push(parsed.error || "Google returned no matches");
  } else {
    errors.push(googlePage.error);
  }

  return {
    query,
    results: [],
    provider: "qsearch",
    error: errors.filter(Boolean).join(" · "),
  };
}

async function cacheGet(
  cache: Cache,
  key: Request,
): Promise<SearchResponse | null> {
  try {
    const hit = await cache.match(key);
    if (!hit) {
      return null;
    }
    const payload = (await hit.json()) as SearchResponse;
    if (!payload?.results?.length) {
      return null;
    }
    return { ...payload, cached: true };
  } catch {
    return null;
  }
}

async function cachePut(
  cache: Cache,
  key: Request,
  payload: SearchResponse,
): Promise<void> {
  try {
    await cache.put(
      key,
      new Response(JSON.stringify(payload), {
        headers: {
          "Content-Type": "application/json",
          "Cache-Control": "public, max-age=300",
        },
      }),
    );
  } catch {
    // ignore
  }
}

function mergeResults(
  primary: SearchResult[],
  secondary: SearchResult[],
  limit: number,
): SearchResult[] {
  const seen = new Set<string>();
  const out: SearchResult[] = [];
  for (const item of [...primary, ...secondary]) {
    if (!item.url) {
      continue;
    }
    let key = item.url;
    try {
      // Lazy import avoided — inline light canonicalize for merge.
      const u = new URL(item.url);
      u.hash = "";
      u.hostname = u.hostname.replace(/^www\./, "").toLowerCase();
      u.pathname = u.pathname.replace(/\/+$/, "") || "/";
      key = u.toString();
    } catch {
      // keep raw
    }
    if (seen.has(key)) {
      continue;
    }
    seen.add(key);
    out.push(item);
    if (out.length >= limit) {
      break;
    }
  }
  return out;
}

export async function runSearch(
  query: string,
  limit: number,
  env: Env,
  ctx?: WorkerExecutionContext,
  page = 1,
  category = "general",
): Promise<SearchResponse> {
  const started = Date.now();
  const trimmed = query.trim();
  const cat = String(category || "general").toLowerCase();
  const searxCategory =
    cat === "all" || cat === "web" || !cat
      ? "general"
      : cat === "maps"
        ? "map"
        : cat;

  if (!trimmed) {
    return {
      query: "",
      results: [],
      provider: "none",
      error: "Missing query",
      tookMs: 0,
      page: 1,
      hasMore: false,
      category: searxCategory,
    };
  }

  const pageNum = Math.max(1, Math.floor(page) || 1);
  const isMedia = searxCategory === "images" || searxCategory === "videos";
  const isMap = searxCategory === "map";
  // Web 18 · media 24 · maps up to 100 (UI default 40).
  const maxForCat = isMap ? 100 : isMedia ? 24 : 18;
  const capped = Math.min(Math.max(limit, 1), maxForCat);
  const fetchLimit = isMap
    ? Math.min(120, Math.max(capped + 20, capped))
    : Math.min(60, Math.max(capped + 8, capped));
  const edgeKey = new Request(
    `https://search-cache.qubrain.internal/qsearch-v23?q=${encodeURIComponent(trimmed)}&limit=${capped}&page=${pageNum}&cat=${searxCategory}`,
  );

  // Edge cache first (fastest), then KV.
  try {
    const edgeHit = await cacheGet(caches.default, edgeKey);
    if (edgeHit) {
      return {
        ...edgeHit,
        page: pageNum,
        category: searxCategory,
        tookMs: Date.now() - started,
      };
    }
  } catch {
    // ignore
  }

  const cached = await readSearchCache(
    env.CACHE,
    trimmed,
    capped,
    pageNum,
    searxCategory,
  );
  if (cached) {
    if (ctx) {
      ctx.waitUntil(cachePut(caches.default, edgeKey, cached));
    }
    return {
      ...cached,
      page: pageNum,
      category: searxCategory,
      tookMs: Date.now() - started,
    };
  }

  // D1 keyword recall is cheap; Vectorize/AI must not own TTFB.
  const d1Promise =
    searxCategory === "general" && pageNum === 1 && env.INDEX
      ? searchOwnIndex(env, trimmed, capped)
      : Promise.resolve([] as SearchResult[]);
  const vectorPromise =
    searxCategory === "general" && pageNum === 1 && env.AI && env.VECTORIZE
      ? withTimeout(
          searchVectorIndex(env, trimmed, Math.min(8, capped)),
          180,
          [] as SearchResult[],
        )
      : Promise.resolve([] as SearchResult[]);

  const searxPromise = env.SEARXNG_URL
    ? searchSearxng(env, trimmed, fetchLimit, pageNum, searxCategory)
    : Promise.resolve({
        query: trimmed,
        results: [] as SearchResult[],
        provider: "qsearch",
        category: searxCategory,
      } satisfies SearchResponse);

  // Dense Photon (+ bias) in parallel so map lists match Brave/Google density.
  const placesPromise =
    isMap && pageNum === 1
      ? searchPlacesDense(trimmed, capped)
      : Promise.resolve([] as SearchResult[]);

  const [ownHits, vectorHits, searxResponse, placeHits] = await Promise.all([
    d1Promise,
    vectorPromise,
    searxPromise,
    placesPromise,
  ]);
  const indexHits = mergeResults(ownHits, vectorHits, capped);

  let serp: SearchResponse = searxResponse;
  let liveHitsForIngest: SearchResult[] = [];
  if (serp.results.length > 0) {
    liveHitsForIngest = serp.results;
  }

  if (
    searxCategory === "general" &&
    pageNum === 1 &&
    serp.results.length === 0 &&
    indexHits.length < Math.min(5, capped) &&
    env.BROWSER
  ) {
    serp = await browserWebSearch(trimmed, fetchLimit, env.BROWSER);
    if (serp.results.length > 0) {
      liveHitsForIngest = serp.results;
    }
  } else if (
    searxCategory === "general" &&
    pageNum === 1 &&
    serp.results.length === 0 &&
    indexHits.length < Math.min(5, capped) &&
    !env.BROWSER &&
    !env.SEARXNG_URL
  ) {
    serp = {
      query: trimmed,
      results: [],
      provider: "qsearch",
      category: searxCategory,
      error:
        "Configure SEARXNG_URL (preferred) or Browser Rendering for live SERP.",
    };
  }

  const merged =
    searxCategory === "general"
      ? mergeResults(indexHits, serp.results, fetchLimit)
      : serp.results.slice(0, fetchLimit);

  let polishedResults =
    searxCategory === "general"
      ? polishResults(trimmed, merged, capped)
      : searxCategory === "news"
        ? sortNewsByPublished(dedupeMedia(merged, capped))
        : dedupeMedia(merged, capped);

  let mapProvider = serp.provider || "qsearch";
  if (isMap) {
    const withGeo = (items: SearchResult[]) =>
      items.filter(
        (r) =>
          typeof r.lat === "number" &&
          typeof r.lon === "number" &&
          Number.isFinite(r.lat) &&
          Number.isFinite(r.lon),
      );
    // Prefer dense/nearby place hits first for maps (address → local POIs).
    const addressExplore = placeHits.some(
      (h) => h.featured || String(h.source || "").includes("nearby"),
    );
    let geoHits = withGeo(placeHits.length ? placeHits : polishedResults);
    const seen = new Set(
      geoHits.map(
        (r) =>
          `${r.lat!.toFixed(5)},${r.lon!.toFixed(5)}|${r.title.toLowerCase()}`,
      ),
    );
    // Don't pad address explores with searx street duplicates.
    if (!addressExplore && pageNum === 1) {
      for (const hit of withGeo(polishedResults)) {
        const key = `${hit.lat!.toFixed(5)},${hit.lon!.toFixed(5)}|${hit.title.toLowerCase()}`;
        if (seen.has(key)) {
          continue;
        }
        seen.add(key);
        geoHits.push(hit);
        if (geoHits.length >= capped) {
          break;
        }
      }
    }
    if (placeHits.length) {
      mapProvider =
        serp.results.length > 0 && !addressExplore
          ? "searxng+photon"
          : "photon";
      if (placeHits.some((h) => String(h.source || "").includes("nearby"))) {
        mapProvider = "nearby+photon";
      }
    }
    polishedResults = geoHits.slice(0, capped);
  }

  const hasMore =
    typeof serp.hasMore === "boolean"
      ? serp.hasMore
      : polishedResults.length >= capped;

  const polished: SearchResponse = {
    query: trimmed,
    results: polishedResults,
    provider:
      searxCategory === "map"
        ? mapProvider
        : searxCategory === "general" && indexHits.length && serp.results.length
          ? "qsearch-hybrid"
          : searxCategory === "general" && indexHits.length
            ? "qsearch-index"
            : serp.provider || "qsearch",
    error: polishedError(serp, merged),
    tookMs: Date.now() - started,
    page: pageNum,
    hasMore,
    category: searxCategory,
    infobox:
      pageNum === 1 && searxCategory === "general" ? serp.infobox : undefined,
  };

  if (liveHitsForIngest.length > 0 && env.INDEX && searxCategory === "general") {
    const ingest = ingestSearchHits(env, liveHitsForIngest, 8);
    if (ctx) {
      ctx.waitUntil(ingest);
    } else {
      await ingest;
    }
  }

  if (polished.results.length > 0) {
    const persist = Promise.all([
      writeSearchCache(
        env.CACHE,
        trimmed,
        capped,
        polished,
        pageNum,
        searxCategory,
      ),
      cachePut(caches.default, edgeKey, polished),
    ]);
    if (ctx) {
      ctx.waitUntil(persist);
    } else {
      await persist;
    }
  }

  return polished;
}

function sortNewsByPublished(results: SearchResult[]): SearchResult[] {
  return [...results].sort((a, b) => {
    const ta = Date.parse(String(a.published || "")) || 0;
    const tb = Date.parse(String(b.published || "")) || 0;
    if (tb !== ta) {
      return tb - ta;
    }
    if (!ta && !tb) {
      return 0;
    }
    if (!ta) {
      return 1;
    }
    if (!tb) {
      return -1;
    }
    return 0;
  });
}

function dedupeMedia(results: SearchResult[], limit: number): SearchResult[] {
  const seen = new Set<string>();
  const out: SearchResult[] = [];
  for (const item of results) {
    if (!item.url || seen.has(item.url)) {
      continue;
    }
    seen.add(item.url);
    out.push(item);
    if (out.length >= limit) {
      break;
    }
  }
  return out;
}

function polishedError(
  serp: SearchResponse,
  merged: SearchResult[],
): string | undefined {
  if (merged.length > 0) {
    return undefined;
  }
  return serp.error;
}
