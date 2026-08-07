import type { Env, SearchResult } from "../types";
import { sanitizeHit } from "../quality";

function docIdFromUrl(url: string): string {
  let hash = 0;
  for (let i = 0; i < url.length; i += 1) {
    hash = (hash * 31 + url.charCodeAt(i)) >>> 0;
  }
  return `doc_${hash.toString(16)}`;
}

function domainOf(url: string): string {
  try {
    return new URL(url).hostname.replace(/^www\./, "");
  } catch {
    return "";
  }
}

/** Keyword search over the owned D1 FTS index. */
export async function searchOwnIndex(
  env: Env,
  query: string,
  limit: number,
): Promise<SearchResult[]> {
  if (!env.INDEX) {
    return [];
  }

  const terms = query
    .trim()
    .split(/\s+/)
    .map((t) => t.replace(/[^a-zA-Z0-9._-]/g, ""))
    .filter((t) => t.length > 1)
    .slice(0, 8);

  if (terms.length === 0) {
    return [];
  }

  const needle = `%${terms.join("%")}%`;
  const first = `%${terms[0]}%`;

  try {
    const rows = await env.INDEX.prepare(
      `SELECT url, title, snippet FROM documents
       WHERE title LIKE ? OR snippet LIKE ? OR domain LIKE ? OR title LIKE ?
       ORDER BY fetched_at DESC
       LIMIT ?`,
    )
      .bind(first, first, first, needle, limit)
      .all<{ url: string; title: string; snippet: string }>();

    return (rows.results || []).map((row: { url: string; title: string; snippet: string }) => ({
      title: row.title,
      url: row.url,
      snippet: row.snippet || "",
      score: 1.15,
    }));
  } catch {
    return [];
  }
}

/** Race a promise against a timeout; resolve with fallback on timeout/error. */
export function withTimeout<T>(
  promise: Promise<T>,
  ms: number,
  fallback: T,
): Promise<T> {
  return new Promise((resolve) => {
    let settled = false;
    const timer = setTimeout(() => {
      if (!settled) {
        settled = true;
        resolve(fallback);
      }
    }, ms);
    promise.then(
      (value) => {
        if (!settled) {
          settled = true;
          clearTimeout(timer);
          resolve(value);
        }
      },
      () => {
        if (!settled) {
          settled = true;
          clearTimeout(timer);
          resolve(fallback);
        }
      },
    );
  });
}

/** Semantic recall via Workers AI embeddings + Vectorize. */
export async function searchVectorIndex(
  env: Env,
  query: string,
  limit: number,
): Promise<SearchResult[]> {
  if (!env.AI || !env.VECTORIZE) {
    return [];
  }

  try {
    const embedded = await env.AI.run("@cf/baai/bge-base-en-v1.5", {
      text: [query],
    });
    const values = embedded?.data?.[0];
    if (!values?.length) {
      return [];
    }

    const matches = await env.VECTORIZE.query(values, {
      topK: limit,
      returnMetadata: "all",
    });

    return (matches.matches || [])
      .filter((m) => m.score >= 0.45)
      .map((m) => {
        const meta = m.metadata || {};
        return {
          title: String(meta.title || m.id),
          url: String(meta.url || ""),
          snippet: String(meta.snippet || ""),
          score: m.score,
        };
      })
      .filter((r) => r.url);
  } catch {
    return [];
  }
}

export async function upsertDocument(
  env: Env,
  doc: { url: string; title: string; snippet?: string },
): Promise<{ id: string }> {
  if (!env.INDEX) {
    throw new Error("INDEX binding missing");
  }

  const id = docIdFromUrl(doc.url);
  const domain = domainOf(doc.url);
  const snippet = doc.snippet || "";
  const fetchedAt = Math.floor(Date.now() / 1000);

  await env.INDEX.prepare(
    `INSERT INTO documents (id, url, title, snippet, domain, fetched_at)
     VALUES (?, ?, ?, ?, ?, ?)
     ON CONFLICT(url) DO UPDATE SET
       title = excluded.title,
       snippet = excluded.snippet,
       domain = excluded.domain,
       fetched_at = excluded.fetched_at`,
  )
    .bind(id, doc.url, doc.title, snippet, domain, fetchedAt)
    .run();

  if (env.AI && env.VECTORIZE) {
    try {
      const text = `${doc.title}\n${snippet}`.slice(0, 1500);
      const embedded = await env.AI.run("@cf/baai/bge-base-en-v1.5", {
        text: [text],
      });
      const values = embedded?.data?.[0];
      if (values?.length) {
        await env.VECTORIZE.upsert([
          {
            id,
            values,
            metadata: {
              url: doc.url,
              title: doc.title,
              snippet,
              domain,
            },
          },
        ]);
      }
    } catch {
      // Vector upsert is best-effort.
    }
  }

  return { id };
}

/** Upsert live SERP hits into D1 + Vectorize so the owned index grows. */
export async function ingestSearchHits(
  env: Env,
  hits: SearchResult[],
  max = 8,
): Promise<number> {
  if (!env.INDEX || hits.length === 0) {
    return 0;
  }

  let count = 0;
  for (const hit of hits.slice(0, max)) {
    const clean = sanitizeHit(hit);
    if (!clean?.url || !clean.title) {
      continue;
    }
    try {
      await upsertDocument(env, {
        url: clean.url,
        title: clean.title,
        snippet: clean.snippet || "",
      });
      count += 1;
    } catch {
      // Best-effort ingest; never fail the search path.
    }
  }
  return count;
}

/** Seed a small starter corpus so own-index has something useful. */
export async function seedStarterCorpus(env: Env): Promise<number> {
  const seeds = [
    {
      url: "https://qubrain.org/",
      title: "QuBrain",
      snippet: "QuBrain platform — products, search, and tools.",
    },
    {
      url: "https://search.qubrain.org/search",
      title: "QSearch",
      snippet: "QSearch — QuBrain web search.",
    },
    {
      url: "https://developers.cloudflare.com/workers/",
      title: "Cloudflare Workers",
      snippet: "Build serverless applications on Cloudflare's global network.",
    },
    {
      url: "https://developer.mozilla.org/en-US/",
      title: "MDN Web Docs",
      snippet: "Resources for developers, by developers.",
    },
    {
      url: "https://www.wikipedia.org/",
      title: "Wikipedia",
      snippet: "The free encyclopedia.",
    },
  ];

  let count = 0;
  for (const seed of seeds) {
    await upsertDocument(env, seed);
    count += 1;
  }
  return count;
}
