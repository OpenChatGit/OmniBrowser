import type { SearchResponse } from "./types";

const CACHE_TTL_SEC = 3600;

function cacheKey(
  query: string,
  limit: number,
  page = 1,
  category = "general",
): string {
  return `qsearch:v23:${category}:${limit}:p${page}:${query.trim().toLowerCase()}`;
}

export async function readSearchCache(
  kv: KVNamespace | undefined,
  query: string,
  limit: number,
  page = 1,
  category = "general",
): Promise<SearchResponse | null> {
  if (!kv) {
    return null;
  }
  try {
    const raw = await kv.get(cacheKey(query, limit, page, category), "json");
    if (!raw || typeof raw !== "object") {
      return null;
    }
    const payload = raw as SearchResponse;
    if (!payload.results?.length) {
      return null;
    }
    return { ...payload, cached: true };
  } catch {
    return null;
  }
}

export async function writeSearchCache(
  kv: KVNamespace | undefined,
  query: string,
  limit: number,
  payload: SearchResponse,
  page = 1,
  category = "general",
): Promise<void> {
  if (!kv || !payload.results?.length) {
    return;
  }
  try {
    await kv.put(
      cacheKey(query, limit, page, category),
      JSON.stringify(payload),
      {
        expirationTtl: CACHE_TTL_SEC,
      },
    );
  } catch {
    // ignore
  }
}
