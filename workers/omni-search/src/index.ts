import { json, withCors } from "./cors";
import { normalizeMediaUrl } from "./media-url";
import { resolvePreviewImage } from "./preview-image";
import { reverseGeocode, enrichMapPlace } from "./geocode";
import { runSearch } from "./providers/serp";
import { seedStarterCorpus, upsertDocument } from "./providers/own-index";
import { QUBRAIN_SVG } from "./render/brand";
import { openSearchXml } from "./render/page";
import { renderSearchPage } from "./render/serp";
import { renderUpdatesPage } from "./render/updates-page";
import { handleUpdatesRequest, listUpdates } from "./updates";
import type { Env, WorkerExecutionContext } from "./types";

const IMAGE_UA =
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36";

function proxyHeaderStrategies(target: URL): HeadersInit[] {
  const accept =
    "image/avif,image/webp,image/apng,image/*,*/*;q=0.8";
  const host = target.hostname.replace(/^www\./, "").toLowerCase();
  const strategies: HeadersInit[] = [];

  // Hotlink CDNs often expect a search-engine referrer.
  if (host.endsWith("mm.bing.net") || host.endsWith("bing.com")) {
    strategies.push({
      Accept: accept,
      "User-Agent": IMAGE_UA,
      Referer: "https://www.bing.com/",
    });
  } else if (host.endsWith("gstatic.com") || host.endsWith("googleusercontent.com")) {
    strategies.push({
      Accept: accept,
      "User-Agent": IMAGE_UA,
      Referer: "https://www.google.com/",
    });
  }

  // Generic: no referrer (Wikimedia / many CDNs), then origin referrer.
  strategies.push(
    { Accept: accept, "User-Agent": IMAGE_UA },
    {
      Accept: accept,
      "User-Agent": IMAGE_UA,
      Referer: `${target.origin}/`,
    },
  );
  return strategies;
}

async function proxyImage(request: Request): Promise<Response> {
  const targetRaw = new URL(request.url).searchParams.get("u") || "";
  const normalized = normalizeMediaUrl(targetRaw);
  let target: URL;
  try {
    target = new URL(normalized || targetRaw);
  } catch {
    return json({ error: "Invalid image URL" }, 400);
  }
  if (!/^https?:$/i.test(target.protocol)) {
    return json({ error: "Unsupported protocol" }, 400);
  }

  let lastStatus = 0;
  try {
    for (const headers of proxyHeaderStrategies(target)) {
      const upstream = await fetch(target.toString(), {
        redirect: "follow",
        signal: AbortSignal.timeout(10_000),
        headers,
        cf: {
          cacheTtl: 86400,
          cacheEverything: true,
        },
      } as RequestInit);
      if (!upstream.ok) {
        lastStatus = upstream.status;
        continue;
      }
      const buffer = await upstream.arrayBuffer();
      if (!buffer.byteLength) {
        lastStatus = 502;
        continue;
      }
      // Cap ~4MB
      if (buffer.byteLength > 4_000_000) {
        return json({ error: "Image too large" }, 413);
      }
      const contentType = upstream.headers.get("content-type") || "";
      if (
        contentType &&
        !contentType.startsWith("image/") &&
        !contentType.includes("svg") &&
        !contentType.includes("octet-stream")
      ) {
        lastStatus = 415;
        continue;
      }
      return withCors(
        new Response(buffer, {
          status: 200,
          headers: {
            "Content-Type": contentType || "image/jpeg",
            "Cache-Control": "public, max-age=86400",
          },
        }),
      );
    }
    return json(
      { error: `Upstream HTTP ${lastStatus || 502}` },
      502,
    );
  } catch (err) {
    return json(
      {
        error:
          err instanceof Error ? `Proxy failed: ${err.message}` : "Proxy failed",
      },
      502,
    );
  }
}

async function handleSearchRequest(
  request: Request,
  env: Env,
  ctx: WorkerExecutionContext,
): Promise<Response> {
  let q = "";
  let limit = 18;
  let page = 1;
  let category = "general";

  if (request.method === "POST") {
    try {
      const body = (await request.json()) as {
        q?: string;
        query?: string;
        limit?: number;
        page?: number;
        category?: string;
        tab?: string;
      };
      q = String(body.q ?? body.query ?? "");
      if (typeof body.limit === "number") {
        limit = body.limit;
      }
      if (typeof body.page === "number") {
        page = body.page;
      }
      category = String(body.category ?? body.tab ?? "general");
    } catch {
      return json({ error: "Invalid JSON body" }, 400);
    }
  } else {
    const url = new URL(request.url);
    q = url.searchParams.get("q") ?? url.searchParams.get("query") ?? "";
    limit = Number(url.searchParams.get("limit") ?? "18");
    page = Number(url.searchParams.get("page") ?? "1");
    category =
      url.searchParams.get("category") ??
      url.searchParams.get("tab") ??
      "general";
  }

  const payload = await runSearch(
    q,
    Number.isFinite(limit) ? limit : 18,
    env,
    ctx,
    Number.isFinite(page) ? page : 1,
    category,
  );
  const status = payload.error && payload.results.length === 0 ? 502 : 200;
  return json(payload, status);
}

export default {
  async fetch(
    request: Request,
    env: Env,
    ctx: WorkerExecutionContext,
  ): Promise<Response> {
    if (request.method === "OPTIONS") {
      return withCors(new Response(null, { status: 204 }));
    }

    const url = new URL(request.url);
    const path = url.pathname.replace(/\/+$/, "") || "/";

    if (path === "/opensearch.xml") {
      return openSearchXml();
    }

    if (path === "/assets/qubrain.svg") {
      return withCors(
        new Response(QUBRAIN_SVG, {
          headers: {
            "Content-Type": "image/svg+xml; charset=utf-8",
            "Cache-Control": "public, max-age=86400",
          },
        }),
      );
    }

    if (
      (path === "/v1/search" || path === "/api/search") &&
      (request.method === "GET" || request.method === "POST")
    ) {
      return handleSearchRequest(request, env, ctx);
    }

    if (
      (path === "/v1/updates" || path === "/api/updates") &&
      request.method === "GET"
    ) {
      const response = await handleUpdatesRequest(env);
      // Short edge cache; KV is source of truth.
      const headers = new Headers(response.headers);
      headers.set("Cache-Control", "public, max-age=60");
      return new Response(response.body, {
        status: response.status,
        statusText: response.statusText,
        headers,
      });
    }

    if (
      (path === "/v1/img" || path === "/api/img") &&
      (request.method === "GET" || request.method === "HEAD")
    ) {
      if (request.method === "HEAD") {
        const full = await proxyImage(
          new Request(request.url, { method: "GET", headers: request.headers }),
        );
        return withCors(
          new Response(null, {
            status: full.status,
            headers: full.headers,
          }),
        );
      }
      return proxyImage(request);
    }

    if (
      (path === "/v1/preview-image" || path === "/api/preview-image") &&
      request.method === "GET"
    ) {
      const target = url.searchParams.get("u") || "";
      const image = await resolvePreviewImage(target, caches.default);
      if (!image) {
        return json({ error: "No preview image" }, 404);
      }
      return withCors(
        new Response(JSON.stringify({ image }), {
          status: 200,
          headers: {
            "Content-Type": "application/json; charset=utf-8",
            "Cache-Control": "public, max-age=3600",
          },
        }),
      );
    }

    if (
      (path === "/v1/geocode/reverse" || path === "/api/geocode/reverse") &&
      request.method === "GET"
    ) {
      const lat = Number(url.searchParams.get("lat"));
      const lon = Number(url.searchParams.get("lon"));
      const name = String(url.searchParams.get("name") || "").trim();
      const osmType = String(url.searchParams.get("osmType") || "").trim();
      const osmIdRaw = url.searchParams.get("osmId");
      const osmId =
        osmIdRaw != null && osmIdRaw !== "" ? Number(osmIdRaw) : undefined;
      const place =
        name || osmType || osmId != null
          ? await enrichMapPlace({
              lat,
              lon,
              name,
              osmType: osmType || undefined,
              osmId: Number.isFinite(osmId as number) ? osmId : undefined,
            })
          : await reverseGeocode(lat, lon);
      if (!place.address && !place.openingHours && !place.name) {
        return json({ error: "No address" }, 404);
      }
      return withCors(
        new Response(JSON.stringify(place), {
          status: 200,
          headers: {
            "Content-Type": "application/json; charset=utf-8",
            "Cache-Control": "public, max-age=3600",
          },
        }),
      );
    }

    if (
      (path === "/v1/place/details" || path === "/api/place/details") &&
      request.method === "GET"
    ) {
      const lat = Number(url.searchParams.get("lat"));
      const lon = Number(url.searchParams.get("lon"));
      const name = String(url.searchParams.get("name") || "").trim();
      const osmType = String(url.searchParams.get("osmType") || "").trim();
      const osmIdRaw = url.searchParams.get("osmId");
      const osmId =
        osmIdRaw != null && osmIdRaw !== "" ? Number(osmIdRaw) : undefined;
      const place = await enrichMapPlace({
        lat,
        lon,
        name,
        osmType: osmType || undefined,
        osmId: Number.isFinite(osmId as number) ? osmId : undefined,
      });
      return withCors(
        new Response(JSON.stringify(place), {
          status: 200,
          headers: {
            "Content-Type": "application/json; charset=utf-8",
            "Cache-Control": "public, max-age=3600",
          },
        }),
      );
    }

    if ((path === "/search" || path === "/") && request.method === "GET") {
      const q = url.searchParams.get("q") ?? "";
      if (!q.trim()) {
        return renderSearchPage({
          query: "",
          results: [],
          provider: "qsearch",
        });
      }
      const category =
        url.searchParams.get("category") ??
        url.searchParams.get("tab") ??
        "general";
      const page = Number(url.searchParams.get("page") ?? "1");
      const cat = String(category || "general").toLowerCase();
      const limit =
        cat === "images" || cat === "videos" || cat === "map" || cat === "maps"
          ? 24
          : 18;
      const payload = await runSearch(
        q,
        limit,
        env,
        ctx,
        Number.isFinite(page) ? page : 1,
        category,
      );
      return renderSearchPage(payload);
    }

    if (
      (path === "/updates" || path === "/info" || path === "/info/updates") &&
      request.method === "GET"
    ) {
      const payload = await listUpdates(env);
      return renderUpdatesPage(payload.updates);
    }

    if (path === "/v1") {
      return json({
        name: "QuBrain Search",
        product: "QuBrain",
        ui: "https://search.qubrain.org/",
        updates: "https://search.qubrain.org/updates",
        home: "Omni Browser",
        api: "https://api.qubrain.org/v1/search",
      });
    }

    // Admin-ish seed/upsert — requires INDEX_ADMIN_KEY via X-Omni-Index-Key.
    if (path === "/v1/index/seed" && request.method === "POST") {
      if (!env.INDEX_ADMIN_KEY) {
        return json({ error: "Index admin disabled" }, 403);
      }
      const key = request.headers.get("X-Omni-Index-Key") || "";
      if (key !== env.INDEX_ADMIN_KEY) {
        return json({ error: "Unauthorized" }, 401);
      }
      const count = await seedStarterCorpus(env);
      return json({ ok: true, seeded: count });
    }

    if (path === "/v1/index/upsert" && request.method === "POST") {
      if (!env.INDEX_ADMIN_KEY) {
        return json({ error: "Index admin disabled" }, 403);
      }
      const key = request.headers.get("X-Omni-Index-Key") || "";
      if (key !== env.INDEX_ADMIN_KEY) {
        return json({ error: "Unauthorized" }, 401);
      }
      try {
        const body = (await request.json()) as {
          url?: string;
          title?: string;
          snippet?: string;
        };
        if (!body.url || !body.title) {
          return json({ error: "url and title required" }, 400);
        }
        const result = await upsertDocument(env, {
          url: body.url,
          title: body.title,
          snippet: body.snippet,
        });
        return json({ ok: true, ...result });
      } catch (err) {
        return json(
          {
            error: err instanceof Error ? err.message : "Upsert failed",
          },
          500,
        );
      }
    }

    return json({ error: "Not found" }, 404);
  },
};
