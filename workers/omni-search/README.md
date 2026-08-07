# QSearch (`omni-search`)

QuBrain's web search product. Omni opens the local QuBrain SERP (`search.html`) and loads organic results from **api.qubrain.org**.

| Surface | URL |
|---------|-----|
| **Omni UI** | local `ui/search.html?q=` (QuBrain branding) |
| **Hosted UI** | `https://search.qubrain.org/search?q=` |
| **REST API** | `https://api.qubrain.org/v1/search` |

## Hybrid model

```
User → QuBrain UI (Omni)
         ↓
      GET api.qubrain.org/v1/search
         ↓
      KV cache (hot queries)
         ↓
      Own index (D1 + Vectorize)  ──┐
         ↓                         ├→ rank → results
      Live SERP (SearXNG Docker) ──┘
         ↓
      Auto-ingest Top-N hits → D1 + Vectorize
         ↓
      Browser Rendering (fallback only)
```

- **Product surface:** QuBrain / QSearch branding
- **Live SERP:** self-hosted [SearXNG](../../infra/searxng/) via `SEARXNG_URL` (JSON API)
- **Own index:** D1 documents + Vectorize / Workers AI embeddings — grows from live hits
- **Fallback:** Browser Rendering scrape only if SearXNG is down/empty and the index is thin
- **Cache:** KV `CACHE` (short TTL)

## Secrets

```bash
# Required for live SERP (tunnel or VPS URL, no trailing slash)
npx wrangler secret put SEARXNG_URL

# Optional Basic Auth (user:pass) or Bearer token for SearXNG
npx wrangler secret put SEARXNG_SECRET
```

Local `wrangler.dev`: put the same keys in `.dev.vars` (e.g. `SEARXNG_URL=http://127.0.0.1:8080`).

See [`infra/searxng/README.md`](../../infra/searxng/README.md) for Docker setup.

## Index APIs

```bash
# Seed a tiny starter corpus
curl -X POST https://api.qubrain.org/v1/index/seed

# Manual upsert
curl -X POST https://api.qubrain.org/v1/index/upsert \
  -H "content-type: application/json" \
  -d '{"url":"https://example.com","title":"Example","snippet":"…"}'
```

Every successful SearXNG (or Browser fallback) search also upserts the top hits into D1/Vectorize in the background (`ctx.waitUntil`), so the Cloudflare index grows from real queries.

## Deploy / smoke

```bash
npx wrangler d1 migrations apply qsearch-index --remote
npx wrangler deploy

# SearXNG must be reachable from the Worker
curl -s "http://127.0.0.1:8080/search?q=test&format=json" | head

curl "https://api.qubrain.org/v1/search?q=cloudflare&limit=5"

# Omni: open QuBrain Search with q=cloudflare — organics from the API, Wikipedia side panel unchanged
```

Dev override for the Omni UI API base (optional):

```js
localStorage.setItem("qubrain.searchApi", "http://127.0.0.1:8787");
```
