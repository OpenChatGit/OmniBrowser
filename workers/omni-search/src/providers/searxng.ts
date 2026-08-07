import type { Env, SearchInfobox, SearchResponse, SearchResult } from "../types";
import { stripTags, unwrapRedirectUrl } from "../html";
import { mediaCandidates } from "../media-url";
import { upgradeNewsThumbUrl } from "../preview-image";
import { canonicalResultUrl, sanitizeHit } from "../quality";

type SearxResult = {
  title?: string;
  url?: string;
  content?: string;
  pretty_url?: string;
  category?: string;
  img_src?: string;
  thumbnail_src?: string;
  thumbnail?: string;
  iframe_src?: string;
  source?: string;
  engine?: string;
  publishedDate?: string;
  pubdate?: string;
  template?: string;
  latitude?: number | string;
  longitude?: number | string;
  boundingbox?: Array<number | string>;
  address?: unknown;
  osm?: { type?: string; id?: number | string };
};

type SearxAttr = {
  label?: string;
  value?: string;
  entity?: string;
};

type SearxUrl = {
  title?: string;
  url?: string;
};

type SearxInfobox = {
  infobox?: string;
  title?: string;
  id?: string;
  content?: string;
  img_src?: string;
  url?: string | null;
  engine?: string;
  attributes?: SearxAttr[];
  urls?: SearxUrl[];
};

type SearxAnswer = {
  answer?: string;
  url?: string;
  engine?: string;
};

type SearxJson = {
  results?: SearxResult[];
  infoboxes?: SearxInfobox[];
  answers?: Array<SearxAnswer | string>;
  error?: string;
  number_of_results?: number;
};

function authHeader(secret?: string): Record<string, string> {
  if (!secret) {
    return {};
  }
  if (secret.includes(":")) {
    const token = btoa(secret);
    return { Authorization: `Basic ${token}` };
  }
  return { Authorization: `Bearer ${secret}` };
}

function normalizeCategory(raw?: string): string {
  const c = String(raw || "general").trim().toLowerCase();
  if (c === "all" || c === "web" || !c) {
    return "general";
  }
  if (c === "maps") {
    return "map";
  }
  if (
    c === "general" ||
    c === "images" ||
    c === "videos" ||
    c === "news" ||
    c === "map" ||
    c === "files" ||
    c === "music" ||
    c === "it" ||
    c === "science"
  ) {
    return c;
  }
  return "general";
}

function mapHit(raw: SearxResult, category: string): SearchResult | null {
  const url = unwrapRedirectUrl(String(raw.url || raw.pretty_url || ""));
  const title = stripTags(String(raw.title || ""));
  if (!url || !title || !/^https?:\/\//i.test(url)) {
    return null;
  }
  try {
    const host = new URL(url).hostname.replace(/^www\./, "").toLowerCase();
    if (
      host === "duckduckgo.com" ||
      host.endsWith(".duckduckgo.com") ||
      host === "bing.com" ||
      host.endsWith(".bing.com")
    ) {
      return null;
    }
  } catch {
    return null;
  }

  const thumbs = mediaCandidates(
    raw.thumbnail_src,
    raw.thumbnail,
    raw.img_src,
  )
    .map((t) => (category === "news" ? upgradeNewsThumbUrl(t) : t))
    .filter(Boolean);
  // Skip decorative icon packs that pollute image SERPs.
  if (
    category === "images" &&
    thumbs.some((t) => /lucide-static|\/icons\/frame\.svg/i.test(t))
  ) {
    return null;
  }

  const thumbnail = thumbs[0];
  const image =
    (category === "news"
      ? upgradeNewsThumbUrl(
          mediaCandidates(raw.img_src, raw.thumbnail_src, raw.thumbnail)[0] ||
            "",
        )
      : mediaCandidates(raw.img_src, raw.thumbnail_src, raw.thumbnail)[0]) ||
    thumbnail;
  const source =
    stripTags(String(raw.source || raw.engine || "")).trim() || undefined;
  const published = String(raw.publishedDate || raw.pubdate || "").trim() || undefined;
  const latRaw = String(raw.latitude ?? "")
    .trim()
    .replace(",", ".");
  const lonRaw = String(raw.longitude ?? "")
    .trim()
    .replace(",", ".");
  const lat = latRaw ? Number(latRaw) : Number.NaN;
  const lon = lonRaw ? Number(lonRaw) : Number.NaN;
  const hasGeo =
    Number.isFinite(lat) &&
    Number.isFinite(lon) &&
    Math.abs(lat) <= 90 &&
    Math.abs(lon) <= 180;
  let address = "";
  if (raw.address && typeof raw.address === "object") {
    const parts = Object.values(raw.address as Record<string, unknown>)
      .map((v) => stripTags(String(v || "")).trim())
      .filter(Boolean);
    address = parts.join(", ");
  } else if (typeof raw.address === "string") {
    address = stripTags(raw.address).trim();
  }
  if (!address && raw.osm?.type && raw.osm?.id != null) {
    // Keep empty — UI reverse-geocodes for a real address instead of node/id.
    address = "";
  }

  const hit = sanitizeHit({
    title,
    url,
    snippet: stripTags(String(raw.content || "")) || address,
    category: raw.category || category,
    thumbnail,
    image,
    thumbnails: thumbs.length ? thumbs : undefined,
    source,
    published,
    ...(hasGeo ? { lat, lon } : {}),
    ...(address ? { address } : {}),
  });

  if (!hit && (category === "images" || category === "videos") && thumbnail) {
    return {
      title,
      url,
      snippet: "",
      category,
      thumbnail,
      image,
      thumbnails: thumbs,
      source,
      published,
    };
  }
  // Map results with coordinates are useful even if sanitize is strict.
  if (
    !hit &&
    category === "map" &&
    hasGeo &&
    title
  ) {
    return {
      title,
      url,
      snippet: address || "",
      category: "map",
      source,
      lat,
      lon,
      address: address || undefined,
    };
  }
  return hit;
}

function mapInfobox(
  boxes: SearxInfobox[] | undefined,
  answers: Array<SearxAnswer | string> | undefined,
): SearchInfobox | undefined {
  const box = (boxes || []).find(
    (b) =>
      stripTags(String(b.infobox || b.title || "")).length > 0 ||
      stripTags(String(b.content || "")).length > 0,
  );

  if (box) {
    const title = stripTags(String(box.infobox || box.title || "")).trim();
    const content = stripTags(String(box.content || "")).trim();
    const wikiUrl =
      (box.urls || []).find((u) => /wikipedia\.org/i.test(String(u.url || "")))
        ?.url ||
      (box.id && /^https?:\/\//i.test(box.id) ? box.id : undefined) ||
      (box.url && /^https?:\/\//i.test(box.url) ? box.url : undefined);

    const attributes = (box.attributes || [])
      .map((a) => ({
        label: stripTags(String(a.label || "")).trim(),
        value: stripTags(String(a.value || "")).trim(),
      }))
      .filter((a) => a.label && a.value)
      .slice(0, 8);

    const urls = (box.urls || [])
      .map((u) => ({
        title: stripTags(String(u.title || "Link")).trim() || "Link",
        url: String(u.url || "").trim(),
      }))
      .filter((u) => /^https?:\/\//i.test(u.url))
      .slice(0, 6);

    if (title || content) {
      return {
        title: title || "Info",
        content,
        image: box.img_src ? String(box.img_src) : undefined,
        url: wikiUrl,
        engine: box.engine ? String(box.engine) : undefined,
        attributes: attributes.length ? attributes : undefined,
        urls: urls.length ? urls : undefined,
      };
    }
  }

  // Fallback: first instant answer
  for (const raw of answers || []) {
    const text =
      typeof raw === "string"
        ? stripTags(raw).trim()
        : stripTags(String(raw.answer || "")).trim();
    if (!text) {
      continue;
    }
    const answerUrl =
      typeof raw === "object" && raw.url && /^https?:\/\//i.test(raw.url)
        ? raw.url
        : undefined;
    return {
      title: "Answer",
      content: text,
      url: answerUrl,
      engine:
        typeof raw === "object" && raw.engine ? String(raw.engine) : undefined,
    };
  }

  return undefined;
}

/** Live SERP via self-hosted SearXNG JSON API. */
export async function searchSearxng(
  env: Env,
  query: string,
  limit: number,
  page = 1,
  category = "general",
): Promise<SearchResponse> {
  const base = env.SEARXNG_URL?.replace(/\/+$/, "");
  if (!base) {
    return {
      query,
      results: [],
      provider: "qsearch",
      error: "SEARXNG_URL not configured",
    };
  }

  const pageno = Math.max(1, Math.floor(page) || 1);
  const cat = normalizeCategory(category);
  // Modest over-fetch for quality filters (avoid doubling upstream work).
  const ask = Math.min(40, Math.max(limit + 6, limit));
  const url =
    `${base}/search?q=${encodeURIComponent(query)}` +
    `&format=json&categories=${encodeURIComponent(cat)}&language=auto&pageno=${pageno}`;

  try {
    const response = await fetch(url, {
      headers: {
        Accept: "application/json",
        ...authHeader(env.SEARXNG_SECRET),
      },
      signal: AbortSignal.timeout(12_000),
    });

    if (!response.ok) {
      const detail = (await response.text()).slice(0, 200);
      return {
        query,
        results: [],
        provider: "qsearch",
        category: cat,
        error: `SearXNG HTTP ${response.status}: ${detail}`,
      };
    }

    const data = (await response.json()) as SearxJson;
    const seen = new Set<string>();
    const results: SearchResult[] = [];

    for (const raw of data.results || []) {
      if (results.length >= ask) {
        break;
      }
      const hit = mapHit(raw, cat);
      if (!hit) {
        continue;
      }
      const key = canonicalResultUrl(hit.url) || hit.url;
      if (seen.has(key)) {
        continue;
      }
      seen.add(key);
      results.push(hit);
    }

    if (cat === "news") {
      results.sort((a, b) => {
        const ta = Date.parse(String(a.published || "")) || 0;
        const tb = Date.parse(String(b.published || "")) || 0;
        if (tb !== ta) {
          return tb - ta; // latest first
        }
        // Undated items after dated ones, keep stable relative order.
        if (ta === 0 && tb === 0) {
          return 0;
        }
        if (ta === 0) {
          return 1;
        }
        if (tb === 0) {
          return -1;
        }
        return 0;
      });
    }

    const infobox =
      pageno === 1 && cat === "general"
        ? mapInfobox(data.infoboxes, data.answers)
        : undefined;

    return {
      query,
      results,
      provider: "qsearch",
      page: pageno,
      category: cat,
      hasMore:
        (data.results?.length || 0) >= 10 ||
        results.length >= limit ||
        (typeof data.number_of_results === "number" &&
          data.number_of_results > pageno * limit),
      infobox,
      error: results.length === 0 ? "SearXNG returned no matches" : undefined,
    };
  } catch (err) {
    return {
      query,
      results: [],
      provider: "qsearch",
      page: pageno,
      category: cat,
      error:
        err instanceof Error
          ? `SearXNG request failed: ${err.message}`
          : "SearXNG request failed",
    };
  }
}
