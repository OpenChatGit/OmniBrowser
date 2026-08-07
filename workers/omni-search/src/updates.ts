import { json } from "./cors";
import type { Env } from "./types";
import seedFile from "../assets/updates.json";

export type AppUpdate = {
  id: string;
  /** ISO date YYYY-MM-DD */
  date: string;
  title: string;
  /** Markdown body shown under the date/title. */
  markdown: string;
};

export type UpdatesPayload = {
  updates: AppUpdate[];
  source: "kv" | "seed";
};

const KV_KEY = "omni:app:updates";

function normalizeUpdates(raw: unknown): AppUpdate[] {
  if (!raw || typeof raw !== "object") {
    return [];
  }
  const list = Array.isArray(raw)
    ? raw
    : Array.isArray((raw as { updates?: unknown }).updates)
      ? (raw as { updates: unknown[] }).updates
      : [];
  const out: AppUpdate[] = [];
  for (const entry of list) {
    if (!entry || typeof entry !== "object") {
      continue;
    }
    const item = entry as Record<string, unknown>;
    const id = String(item.id || "").trim();
    const date = String(item.date || "").trim();
    const title = String(item.title || "").trim();
    const markdown = String(item.markdown || item.body || "").trim();
    if (!date || !markdown) {
      continue;
    }
    out.push({
      id: id || `${date}-${out.length + 1}`,
      date,
      title: title || "Update",
      markdown,
    });
  }
  out.sort((a, b) => String(b.date).localeCompare(String(a.date)));
  return out;
}

/** Seed written to KV on miss — keep in sync with assets/updates.json. */
const SEED_UPDATES: AppUpdate[] = normalizeUpdates(seedFile);

export async function listUpdates(env: Env): Promise<UpdatesPayload> {
  if (env.CACHE) {
    try {
      const stored = await env.CACHE.get(KV_KEY, "json");
      const updates = normalizeUpdates(stored);
      if (updates.length > 0) {
        return { updates, source: "kv" };
      }
    } catch {
      // fall through to seed
    }
  }

  const seeded = SEED_UPDATES.slice();
  if (env.CACHE) {
    try {
      await env.CACHE.put(KV_KEY, JSON.stringify({ updates: seeded }));
    } catch {
      // ignore write failures; still return seed
    }
  }
  return { updates: seeded, source: "seed" };
}

export async function handleUpdatesRequest(env: Env): Promise<Response> {
  const payload = await listUpdates(env);
  return json(payload, 200);
}
