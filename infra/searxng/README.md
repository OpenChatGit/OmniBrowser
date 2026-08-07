# SearXNG (QSearch live SERP)

Private SearXNG instance used by the Cloudflare `omni-search` Worker. The owned D1/Vectorize index lives on Cloudflare; SearXNG supplies live hits.

## Automatic public URL (no manual tunnel)

A **named** Cloudflare Tunnel is wired in Docker Compose:

| Piece | Value |
|-------|--------|
| Public URL | `https://searx.qubrain.org` |
| Tunnel name | `qsearch-searxng` |
| Origin | `http://searxng:8080` (Docker network) |

Once `.env` has `TUNNEL_TOKEN`, start everything:

```bash
cd infra/searxng
# First time only: copy .env.example → .env and paste the tunnel token
docker compose up -d
```

That starts **SearXNG + cloudflared** together. No more `cloudflared tunnel --url …` in a separate window.

Local healthcheck:

```bash
curl -s "http://127.0.0.1:8080/search?q=test&format=json" | head
```

Public (what the Worker uses):

```bash
curl -s "https://searx.qubrain.org/search?q=test&format=json" | head
```

After changing `settings.yml` / `limiter.toml`:

```bash
docker compose up -d --force-recreate
```

Startup: `ahmia` / `torch` / `wikidata` are removed in `settings.yml` (onion / SPARQL issues). General web search still works.

## Wire Worker once

```bash
cd workers/omni-search
npx wrangler secret put SEARXNG_URL
# value: https://searx.qubrain.org
```

Optional Basic Auth:

```bash
npx wrangler secret put SEARXNG_SECRET
```

Local `wrangler.dev` without tunnel: `SEARXNG_URL=http://127.0.0.1:8080` in `.dev.vars`.

## Smoke

1. `docker compose up -d`
2. Public JSON curl above
3. `curl "https://api.qubrain.org/v1/search?q=cloudflare&limit=5"`
4. Omni QuBrain Search
