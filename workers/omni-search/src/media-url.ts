/** Wikimedia production thumbnail steps (https://w.wiki/GHai). */
const WIKI_THUMB_STEPS = [
  20, 40, 60, 120, 250, 330, 500, 960, 1280, 1920, 3840,
] as const;

function nearestWikiThumbWidth(width: number): number {
  if (!Number.isFinite(width) || width <= 0) {
    return 250;
  }
  for (const step of WIKI_THUMB_STEPS) {
    if (width <= step) {
      return step;
    }
  }
  return WIKI_THUMB_STEPS[WIKI_THUMB_STEPS.length - 1];
}

/**
 * Normalize media URLs for SERP thumbnails:
 * - protocol-relative → https
 * - http → https
 * - collapse accidental `//` in paths (IIIF etc.)
 * - snap Wikimedia `/NNpx-` thumbs to allowed sizes
 */
export function normalizeMediaUrl(raw: string): string {
  let value = String(raw || "").trim();
  if (!value) {
    return "";
  }
  if (value.startsWith("//")) {
    value = `https:${value}`;
  }
  if (value.startsWith("http://")) {
    value = `https://${value.slice("http://".length)}`;
  }
  try {
    const parsed = new URL(value);
    if (!/^https?:$/i.test(parsed.protocol)) {
      return "";
    }
    parsed.pathname = parsed.pathname.replace(/\/{2,}/g, "/");

    const host = parsed.hostname.replace(/^www\./, "").toLowerCase();
    if (
      (host === "upload.wikimedia.org" || host.endsWith(".wikimedia.org")) &&
      parsed.pathname.includes("/thumb/")
    ) {
      parsed.pathname = parsed.pathname.replace(
        /\/(\d+)px-([^/]+)$/i,
        (_m, px: string, file: string) => {
          const next = nearestWikiThumbWidth(Number(px));
          return `/${next}px-${file}`;
        },
      );
    }

    return parsed.toString();
  } catch {
    return "";
  }
}

export function mediaCandidates(
  ...urls: Array<string | undefined>
): string[] {
  const out: string[] = [];
  const seen = new Set<string>();
  for (const raw of urls) {
    const value = normalizeMediaUrl(String(raw || ""));
    if (!value || seen.has(value)) {
      continue;
    }
    seen.add(value);
    out.push(value);
  }
  return out;
}
