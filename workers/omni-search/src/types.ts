export interface SearchResult {
  title: string;
  url: string;
  snippet: string;
  /** Official / navigational hit shown like Google's top brand result. */
  featured?: boolean;
  /** Optional score for merge ranking. */
  score?: number;
  category?: string;
  /** Image / video thumbnail URL. */
  thumbnail?: string;
  /** Full-size image URL (images category). */
  image?: string;
  /** Ordered fallback image URLs (thumbnail → full → …). */
  thumbnails?: string[];
  source?: string;
  /** ISO / displayable publish time from news engines. */
  published?: string;
  /** Map / geo result latitude. */
  lat?: number;
  /** Map / geo result longitude. */
  lon?: number;
  /** Human-readable address for map results. */
  address?: string;
  /** OSM opening_hours string when available. */
  openingHours?: string;
  /** Phone / website from OSM tags when available. */
  phone?: string;
  website?: string;
  /** OSM element type: node | way | relation */
  osmType?: string;
  /** OSM element id */
  osmId?: number;
  /** OSM class/value e.g. shop=supermarket, amenity=cafe */
  placeType?: string;
}

export interface SearchInfobox {
  title: string;
  content: string;
  image?: string;
  url?: string;
  engine?: string;
  attributes?: Array<{ label: string; value: string }>;
  urls?: Array<{ title: string; url: string }>;
}

export interface SearchResponse {
  query: string;
  results: SearchResult[];
  provider: string;
  error?: string;
  tookMs?: number;
  cached?: boolean;
  /** 1-based page index. */
  page?: number;
  /** True when another page of results is likely available. */
  hasMore?: boolean;
  /** Knowledge panel from SearXNG (Wikipedia / Wikidata / …). */
  infobox?: SearchInfobox;
  /** Active SearXNG category (general, images, videos, news, map). */
  category?: string;
}

export type OfficialSite = {
  url: string;
  title: string;
  snippet: string;
};

export interface BrowserBinding {
  quickAction(
    action: "content" | "scrape" | "screenshot" | "markdown" | "json" | "links",
    options: Record<string, unknown>,
  ): Promise<Response>;
}

export interface AiBinding {
  run(
    model: string,
    input: Record<string, unknown>,
  ): Promise<{ data?: number[][]; shape?: number[] }>;
}

export interface VectorizeMatch {
  id: string;
  score: number;
  metadata?: Record<string, unknown>;
}

export interface VectorizeIndex {
  query(
    vector: number[],
    options?: { topK?: number; returnMetadata?: string },
  ): Promise<{ matches: VectorizeMatch[] }>;
  upsert(
    vectors: Array<{
      id: string;
      values: number[];
      metadata?: Record<string, unknown>;
    }>,
  ): Promise<unknown>;
}

export interface Env {
  BROWSER?: BrowserBinding;
  CACHE?: KVNamespace;
  INDEX?: D1Database;
  VECTORIZE?: VectorizeIndex;
  AI?: AiBinding;
  /** Base URL of the self-hosted SearXNG instance (no trailing slash). */
  SEARXNG_URL?: string;
  /** Optional Basic `user:pass` or Bearer token for SearXNG. */
  SEARXNG_SECRET?: string;
  /** Shared secret for /v1/index/* admin writes (header X-Omni-Index-Key). */
  INDEX_ADMIN_KEY?: string;
}

/** Minimal Workers execution context for background ingest. */
export interface WorkerExecutionContext {
  waitUntil(promise: Promise<unknown>): void;
}
