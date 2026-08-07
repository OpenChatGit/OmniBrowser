import { normalizeMediaUrl } from "./media-url";

function extractPreviewUrl(html: string, pageUrl: string): string {
  const slice = html.slice(0, 120_000);
  const patterns = [
    /property=["']og:image:secure_url["'][^>]*content=["']([^"']+)["']/i,
    /content=["']([^"']+)["'][^>]*property=["']og:image:secure_url["']/i,
    /property=["']og:image["'][^>]*content=["']([^"']+)["']/i,
    /content=["']([^"']+)["'][^>]*property=["']og:image["']/i,
    /name=["']twitter:image(?::src)?["'][^>]*content=["']([^"']+)["']/i,
    /content=["']([^"']+)["'][^>]*name=["']twitter:image(?::src)?["']/i,
    /rel=["']image_src["'][^>]*href=["']([^"']+)["']/i,
    /href=["']([^"']+)["'][^>]*rel=["']image_src["']/i,
  ];
  for (const re of patterns) {
    const match = slice.match(re);
    if (!match?.[1]) {
      continue;
    }
    let raw = match[1].trim();
    if (!raw || raw.startsWith("data:")) {
      continue;
    }
    try {
      raw = new URL(raw, pageUrl).toString();
    } catch {
      continue;
    }
    const normalized = normalizeMediaUrl(raw);
    if (normalized) {
      return normalized;
    }
  }
  return "";
}

/** Prefer a sharper variant when publishers ship tiny SERP thumbs. */
export function upgradeNewsThumbUrl(raw: string): string {
  const value = normalizeMediaUrl(raw);
  if (!value) {
    return "";
  }
  try {
    const u = new URL(value);
    if (/reuters\.com$/i.test(u.hostname.replace(/^www\./, ""))) {
      const h = Number(u.searchParams.get("height") || "0");
      if (h > 0 && h < 200) {
        u.searchParams.set("height", "240");
      }
    }
    return u.toString();
  } catch {
    return value;
  }
}

export async function resolvePreviewImage(
  pageUrlRaw: string,
  cache?: Cache,
): Promise<string> {
  let pageUrl: URL;
  try {
    pageUrl = new URL(pageUrlRaw);
  } catch {
    return "";
  }
  if (!/^https?:$/i.test(pageUrl.protocol)) {
    return "";
  }

  const cacheKey = new Request(
    `https://preview-cache.qubrain.internal/v1?u=${encodeURIComponent(pageUrl.toString())}`,
  );
  if (cache) {
    try {
      const hit = await cache.match(cacheKey);
      if (hit) {
        const data = (await hit.json()) as { image?: string };
        if (data?.image) {
          return String(data.image);
        }
      }
    } catch {
      // ignore
    }
  }

  try {
    const upstream = await fetch(pageUrl.toString(), {
      redirect: "follow",
      signal: AbortSignal.timeout(2500),
      headers: {
        Accept: "text/html,application/xhtml+xml;q=0.9,*/*;q=0.8",
        "User-Agent":
          "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36",
      },
    });
    if (!upstream.ok) {
      return "";
    }
    const ctype = upstream.headers.get("content-type") || "";
    if (ctype && !/html|xml/i.test(ctype)) {
      return "";
    }
    const html = await upstream.text();
    const image = extractPreviewUrl(html, pageUrl.toString());
    if (image && cache) {
      try {
        await cache.put(
          cacheKey,
          new Response(JSON.stringify({ image }), {
            headers: {
              "Content-Type": "application/json",
              "Cache-Control": "public, max-age=86400",
            },
          }),
        );
      } catch {
        // ignore
      }
    }
    return image;
  } catch {
    return "";
  }
}
