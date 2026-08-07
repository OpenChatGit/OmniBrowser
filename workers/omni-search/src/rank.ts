import type { OfficialSite, SearchResult } from "./types";
import { OFFICIAL_SITES, lookupOfficial } from "./official";
import {
  isHomepageUrl,
  resultDedupeKey,
  resultHostKey,
  sanitizeHit,
} from "./quality";

function registrableHint(hostname: string): string {
  const host = hostname.replace(/^www\./, "").toLowerCase();
  const parts = host.split(".");
  if (parts.length >= 2) {
    return parts[parts.length - 2];
  }
  return host;
}

function isSocialOrProfileUrl(url: URL): boolean {
  const host = url.hostname.replace(/^www\./, "").toLowerCase();
  const path = url.pathname.toLowerCase().replace(/\/+$/, "") || "/";
  if (host === "youtube.com" || host === "m.youtube.com" || host === "youtu.be") {
    if (path.startsWith("/@") || path.startsWith("/channel/") || path.startsWith("/user/")) {
      return true;
    }
    if (/^\/[a-z0-9._-]{2,40}$/i.test(path) && path !== "/") {
      return true;
    }
  }
  if (host === "facebook.com" || host === "fb.com") {
    return path.length > 1 && !path.startsWith("/login");
  }
  if (
    host === "instagram.com" ||
    host === "tiktok.com" ||
    host === "x.com" ||
    host === "twitter.com"
  ) {
    return path.length > 1;
  }
  if (host === "linkedin.com") {
    return path.startsWith("/company/") || path.startsWith("/in/");
  }
  return false;
}

function isCountryOrLocaleDomain(host: string, brand: string): boolean {
  const h = host.replace(/^www\./, "").toLowerCase();
  if (h === `${brand}.com` || h === brand) {
    return false;
  }
  return (
    h.startsWith(`${brand}.`) ||
    h.includes(`.${brand}.`) ||
    h.endsWith(`.${brand}`)
  );
}

function isHomepage(url: URL): boolean {
  const path = url.pathname.replace(/\/+$/, "") || "/";
  return path === "/" && !url.search;
}

export function scoreResult(
  query: string,
  result: SearchResult,
  official: OfficialSite | null,
): number {
  let score = typeof result.score === "number" ? result.score : 0;
  let url: URL;
  try {
    url = new URL(result.url);
  } catch {
    return -1000;
  }

  const host = url.hostname.replace(/^www\./, "").toLowerCase();
  const q = query.trim().toLowerCase();
  const brandKey = official
    ? Object.entries(OFFICIAL_SITES).find(([, v]) => v.url === official.url)?.[0] || q
    : q;

  if (isSocialOrProfileUrl(url)) {
    score -= 800;
  }

  // Bare social homepage is only useful for navigational brand queries.
  if (
    isHomepage(url) &&
    (host === "youtube.com" ||
      host === "facebook.com" ||
      host === "instagram.com" ||
      host === "tiktok.com") &&
    !official
  ) {
    score -= 200;
  }

  if (!result.snippet) {
    score -= 40;
  }

  if (official) {
    const officialHost = new URL(official.url).hostname
      .replace(/^www\./, "")
      .toLowerCase();
    if (host === officialHost && isHomepage(url)) {
      score += 10000;
    } else if (host === officialHost) {
      score += 2500;
    } else if (host.endsWith(`.${officialHost}`)) {
      score += 400;
    } else if (isCountryOrLocaleDomain(host, brandKey)) {
      score -= 600;
    }
  } else if (/^[a-z0-9][a-z0-9-]{1,40}$/i.test(q)) {
    if ((host === `${q}.com` || host === q) && isHomepage(url)) {
      score += 9000;
    } else if (host === `${q}.com` || host === q) {
      score += 2000;
    } else if (registrableHint(host) === q && isSocialOrProfileUrl(url)) {
      score -= 700;
    }
  }

  const title = result.title.toLowerCase();
  if (title === q || title.startsWith(`${q} `) || title.startsWith(`${q} -`)) {
    score += 120;
  }

  if (official || /^[a-z0-9-]+$/i.test(q)) {
    if (host.includes("wikipedia.org")) score -= 80;
    if (host.startsWith("accounts.") || host.startsWith("login.")) score -= 200;
    if (host.startsWith("ogs.") || host.includes("empty")) score -= 400;
  }

  return score;
}

export function polishResults(
  query: string,
  results: SearchResult[],
  limit: number,
): SearchResult[] {
  const official = lookupOfficial(query);
  const cleaned = results
    .map((r) => sanitizeHit(r))
    .filter((r): r is SearchResult => Boolean(r));

  const scored = cleaned.map((result, index) => ({
    result,
    index,
    score: scoreResult(query, result, official),
  }));

  scored.sort((a, b) => b.score - a.score || a.index - b.index);

  let polished: SearchResult[] = scored
    .filter((s) => s.score > -500)
    .map((s) => ({
      ...s.result,
      featured: false,
    }));

  if (official) {
    const officialHost = new URL(official.url).hostname
      .replace(/^www\./, "")
      .toLowerCase();
    const existingIdx = polished.findIndex((r) => {
      try {
        const u = new URL(r.url);
        return (
          u.hostname.replace(/^www\./, "").toLowerCase() === officialHost &&
          isHomepage(u)
        );
      } catch {
        return false;
      }
    });

    const featured: SearchResult = {
      title: official.title,
      url: official.url,
      snippet: official.snippet,
      featured: true,
    };

    if (existingIdx >= 0) {
      const existing = polished[existingIdx];
      polished.splice(existingIdx, 1);
      polished.unshift({
        ...featured,
        // Always keep curated official copy — SERP snippets are often footer/locale junk.
        snippet: featured.snippet,
        title: featured.title || existing.title,
        url: featured.url,
      });
    } else {
      polished.unshift(featured);
    }
  } else if (polished.length > 0 && scoreResult(query, polished[0], null) >= 8000) {
    polished[0] = { ...polished[0], featured: true };
  }

  const seenKeys = new Set<string>();
  const seenHomepages = new Set<string>();
  const deduped: SearchResult[] = [];
  for (const item of polished) {
    if (deduped.length >= limit) {
      break;
    }
    try {
      const key = resultDedupeKey(item.url);
      if (!key || seenKeys.has(key)) {
        continue;
      }
      const host = resultHostKey(item.url);
      if (isHomepageUrl(item.url) && host) {
        if (seenHomepages.has(host)) {
          continue;
        }
        seenHomepages.add(host);
      }
      if (official) {
        const brand = Object.entries(OFFICIAL_SITES).find(
          ([, v]) => v.url === official.url,
        )?.[0];
        const offHost = new URL(official.url).hostname
          .replace(/^www\./, "")
          .toLowerCase();
        if (
          brand &&
          host !== offHost &&
          !host.endsWith(`.${offHost}`) &&
          isCountryOrLocaleDomain(host, brand)
        ) {
          continue;
        }
      }
      seenKeys.add(key);
      deduped.push(item);
    } catch {
      deduped.push(item);
    }
  }

  return deduped;
}
