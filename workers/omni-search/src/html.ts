export function escapeHtml(value: string): string {
  return value
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

export function decodeBasicEntities(value: string): string {
  return value
    .replace(/&#x27;/gi, "'")
    .replace(/&#39;/g, "'")
    .replace(/&amp;/g, "&")
    .replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">")
    .replace(/&quot;/g, '"')
    .replace(/&nbsp;/g, " ")
    .replace(/&#(\d+);/g, (_, n) => String.fromCharCode(Number(n)))
    .replace(/&#x([0-9a-f]+);/gi, (_, h) =>
      String.fromCharCode(parseInt(h, 16)),
    );
}

export function stripTags(value: string): string {
  return decodeBasicEntities(
    value.replace(/<[^>]+>/g, " ").replace(/\s+/g, " ").trim(),
  );
}

export function unwrapRedirectUrl(raw: string): string {
  const href = decodeBasicEntities(raw.trim());
  if (!href) {
    return "";
  }
  try {
    const absolute = href.startsWith("//")
      ? `https:${href}`
      : href.startsWith("/")
        ? `https://duckduckgo.com${href}`
        : href;
    const parsed = new URL(absolute);
    const host = parsed.hostname.replace(/^www\./, "").toLowerCase();

    // Google click-tracking redirects: /url?q=https://...
    if (
      (host === "google.com" || host.endsWith(".google.com")) &&
      parsed.pathname.startsWith("/url")
    ) {
      const dest =
        parsed.searchParams.get("q") || parsed.searchParams.get("url");
      if (dest && /^https?:\/\//i.test(dest)) {
        return dest;
      }
    }

    for (const key of ["uddg", "u", "url"]) {
      const nested = parsed.searchParams.get(key);
      if (nested && /^https?:\/\//i.test(nested)) {
        return nested;
      }
    }
    // Drop search-engine chrome pages, keep real destinations (incl. google.com).
    if (
      host === "duckduckgo.com" ||
      host.endsWith(".duckduckgo.com") ||
      host === "bing.com" ||
      host.endsWith(".bing.com") ||
      ((host === "google.com" || host.endsWith(".google.com")) &&
        parsed.pathname.startsWith("/search"))
    ) {
      return "";
    }
    return absolute;
  } catch {
    return href.startsWith("http") ? href : "";
  }
}

export function attr(tag: string, name: string): string {
  const re = new RegExp(`${name}\\s*=\\s*["']([^"']*)["']`, "i");
  return tag.match(re)?.[1] ?? "";
}
