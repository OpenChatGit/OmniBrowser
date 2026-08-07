import type { SearchResult } from "./types";
import { unwrapRedirectUrl } from "./html";

const TRACKING_PARAMS = new Set([
  "utm_source",
  "utm_medium",
  "utm_campaign",
  "utm_term",
  "utm_content",
  "fbclid",
  "gclid",
  "mc_cid",
  "mc_eid",
  "si",
  "feature",
  "ref",
  "ref_src",
  // Locale / geo chrome that creates fake homepage dupes
  "gl",
  "hl",
  "lang",
  "locale",
  "country",
  "region",
  "app",
]);

/** Normalize URLs so www / trailing slash / tracking variants dedupe. */
export function canonicalResultUrl(raw: string): string {
  const unwrapped = unwrapRedirectUrl(raw) || raw;
  try {
    const u = new URL(unwrapped);
    if (!/^https?:$/i.test(u.protocol)) {
      return "";
    }
    u.hash = "";
    u.hostname = u.hostname.replace(/^www\./, "").toLowerCase();
    // Drop common tracking noise.
    for (const key of [...u.searchParams.keys()]) {
      if (TRACKING_PARAMS.has(key.toLowerCase()) || key.toLowerCase().startsWith("utm_")) {
        u.searchParams.delete(key);
      }
    }
    let path = u.pathname.replace(/\/+$/, "") || "/";
    // Treat index.html as homepage.
    if (/\/index\.(html?|php|aspx?)$/i.test(path)) {
      path = "/";
    }
    u.pathname = path;
    // Stable query order
    const entries = [...u.searchParams.entries()].sort((a, b) =>
      a[0] === b[0] ? a[1].localeCompare(b[1]) : a[0].localeCompare(b[0]),
    );
    u.search = "";
    for (const [k, v] of entries) {
      u.searchParams.append(k, v);
    }
    return u.toString();
  } catch {
    return "";
  }
}

/** Footer / nav glue like "AboutPressCopyrightContact usCreators…" */
export function isJunkSnippet(snippet: string): boolean {
  const s = String(snippet || "").replace(/\s+/g, " ").trim();
  if (!s) {
    return false;
  }
  // CamelCase words glued without spaces (AboutPressCopyright)
  if (/[a-z][A-Z][a-z]+[A-Z]/.test(s) && s.split(/\s+/).length < 8) {
    return true;
  }
  if (
    /AboutPress|PressCopyright|CopyrightContact|Contact usCreators|CreatorsAdvertise|AdvertiseDevelopers|TermsPrivacy|PrivacyPolicy|Policy\s*&\s*SafetyHow|How YouTube worksTest/i.test(
      s,
    )
  ) {
    return true;
  }
  // Dense footer copyright blobs
  if (/©\s*\d{4}.{0,40}(LLC|Inc|GmbH)/i.test(s) && /Privacy|Terms|Cookie/i.test(s)) {
    return true;
  }
  // Almost no spaces but long → scraped menu
  const letters = s.replace(/[^a-zA-Z]/g, "");
  const spaces = (s.match(/\s/g) || []).length;
  if (letters.length > 60 && spaces < 4) {
    return true;
  }
  return false;
}

export function isBrokenOrUselessUrl(raw: string): boolean {
  const canon = canonicalResultUrl(raw);
  if (!canon) {
    return true;
  }
  try {
    const u = new URL(canon);
    const host = u.hostname;
    if (!host || host === "localhost" || /^\d+\.\d+\.\d+\.\d+$/.test(host)) {
      return true;
    }
    // Empty / void destinations
    if (u.protocol === "javascript:" || u.protocol === "data:") {
      return true;
    }
    // Soft-404 / placeholder paths often surfaced by meta engines
    const path = u.pathname.toLowerCase();
    if (
      path.includes("/404") ||
      path.includes("/not-found") ||
      path.includes("/error") ||
      path.endsWith("/undefined") ||
      path.endsWith("/null") ||
      // YouTube chrome surfaces that are not real destinations
      path === "/feed/homepage" ||
      path === "/feed/history" ||
      path === "/feed/storefront"
    ) {
      return true;
    }
    return false;
  } catch {
    return true;
  }
}

/**
 * Drop or repair low-quality SERP hits before ranking.
 * Returns null when the hit should be discarded.
 */
export function sanitizeHit(hit: SearchResult): SearchResult | null {
  if (!hit?.title || !hit?.url) {
    return null;
  }
  if (isBrokenOrUselessUrl(hit.url)) {
    return null;
  }
  const url = canonicalResultUrl(hit.url) || hit.url;
  let snippet = String(hit.snippet || "").replace(/\s+/g, " ").trim();
  if (isJunkSnippet(snippet)) {
    snippet = "";
  }
  const title = String(hit.title).replace(/\s+/g, " ").trim();
  if (title.length < 2) {
    return null;
  }
  // Title that is just a URL / host with empty junk page
  if (/^https?:\/\//i.test(title) && !snippet) {
    return null;
  }
  return {
    ...hit,
    url,
    title,
    snippet,
  };
}

/** Host+path key used for near-duplicate collapse. */
export function resultDedupeKey(url: string): string {
  const canon = canonicalResultUrl(url);
  if (!canon) {
    return url;
  }
  try {
    const u = new URL(canon);
    return `${u.hostname}${u.pathname}${u.search}`;
  } catch {
    return canon;
  }
}

/** Registrable-ish host for one-homepage-per-site collapse. */
export function resultHostKey(url: string): string {
  try {
    return new URL(canonicalResultUrl(url) || url).hostname.replace(/^www\./, "");
  } catch {
    return "";
  }
}

export function isHomepageUrl(url: string): boolean {
  try {
    const u = new URL(canonicalResultUrl(url) || url);
    const path = u.pathname.replace(/\/+$/, "") || "/";
    return path === "/" && !u.search;
  } catch {
    return false;
  }
}
