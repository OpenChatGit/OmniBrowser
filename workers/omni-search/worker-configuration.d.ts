/* Minimal Cloudflare Worker ambient types for QSearch. */

interface KVNamespace {
  get(
    key: string,
    type: "json",
  ): Promise<unknown | null>;
  get(key: string, type?: "text"): Promise<string | null>;
  put(
    key: string,
    value: string,
    options?: { expirationTtl?: number },
  ): Promise<void>;
}

interface D1PreparedStatement {
  bind(...values: unknown[]): D1PreparedStatement;
  first<T = unknown>(): Promise<T | null>;
  run(): Promise<unknown>;
  all<T = unknown>(): Promise<{ results: T[] }>;
}

interface D1Database {
  prepare(query: string): D1PreparedStatement;
}

interface BrowserBinding {
  quickAction(
    action: string,
    options: Record<string, unknown>,
  ): Promise<Response>;
}

interface AiBinding {
  run(
    model: string,
    input: Record<string, unknown>,
  ): Promise<{ data?: number[][]; shape?: number[] }>;
}

interface VectorizeMatch {
  id: string;
  score: number;
  metadata?: Record<string, unknown>;
}

interface VectorizeIndex {
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

interface Env {
  BROWSER?: BrowserBinding;
  CACHE?: KVNamespace;
  INDEX?: D1Database;
  VECTORIZE?: VectorizeIndex;
  AI?: AiBinding;
  SEARXNG_URL?: string;
  SEARXNG_SECRET?: string;
}

interface CacheStorage {
  readonly default: Cache;
}
