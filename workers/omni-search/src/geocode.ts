import type { SearchResult } from "./types";

const NOMINATIM_UA = "QuBrainSearch/1.0 (https://qubrain.org)";

/** Parse lat/lon that may use comma decimals. */
export function parseCoord(raw: unknown): number {
  if (typeof raw === "number") {
    return Number.isFinite(raw) ? raw : NaN;
  }
  const text = String(raw ?? "")
    .trim()
    .replace(",", ".");
  if (!text) {
    return NaN;
  }
  const n = Number(text);
  return Number.isFinite(n) ? n : NaN;
}

function shortAddressFromParts(
  addr: Record<string, string> | undefined,
  fallback: string,
): string {
  const a = addr || {};
  const shortBits = [
    a.road || a.pedestrian || a.footway || a.neighbourhood,
    a.house_number,
    a.city || a.town || a.village || a.municipality,
    a.country,
  ]
    .map((v) => String(v || "").trim())
    .filter(Boolean);
  if (shortBits.length >= 2) {
    return [shortBits[0], shortBits.slice(1).join(", ")].join(
      a.house_number ? " " : ", ",
    );
  }
  return fallback;
}

export type GeocodePlace = {
  address: string;
  name: string;
  openingHours: string;
  phone: string;
  website: string;
  wikipedia: string;
  placeType?: string;
};

/** Reverse-geocode via Nominatim (OpenStreetMap). */
export async function reverseGeocode(
  lat: number,
  lon: number,
): Promise<GeocodePlace> {
  const empty: GeocodePlace = {
    address: "",
    name: "",
    openingHours: "",
    phone: "",
    website: "",
    wikipedia: "",
  };
  if (
    !Number.isFinite(lat) ||
    !Number.isFinite(lon) ||
    Math.abs(lat) > 90 ||
    Math.abs(lon) > 180
  ) {
    return empty;
  }

  try {
    const url = new URL("https://nominatim.openstreetmap.org/reverse");
    url.searchParams.set("format", "jsonv2");
    url.searchParams.set("lat", String(lat));
    url.searchParams.set("lon", String(lon));
    url.searchParams.set("zoom", "18");
    url.searchParams.set("addressdetails", "1");
    url.searchParams.set("extratags", "1");
    url.searchParams.set("namedetails", "0");

    const upstream = await fetch(url.toString(), {
      redirect: "follow",
      signal: AbortSignal.timeout(3000),
      headers: {
        Accept: "application/json",
        "User-Agent": NOMINATIM_UA,
      },
    });
    if (!upstream.ok) {
      return empty;
    }
    const data = (await upstream.json()) as {
      display_name?: string;
      name?: string;
      extratags?: Record<string, string>;
      address?: Record<string, string>;
    };
    const tags = data.extratags || {};

    return {
      address: shortAddressFromParts(
        data.address,
        String(data.display_name || "").trim(),
      ),
      name: String(data.name || "").trim(),
      openingHours: String(
        tags.opening_hours || tags["opening_hours:covid19"] || "",
      ).trim(),
      phone: String(tags.phone || tags["contact:phone"] || "").trim(),
      website: String(tags.website || tags["contact:website"] || "").trim(),
      wikipedia: String(tags.wikipedia || "").trim(),
    };
  } catch {
    return empty;
  }
}

type NominatimSearchHit = {
  lat?: string;
  lon?: string;
  display_name?: string;
  name?: string;
  type?: string;
  class?: string;
  osm_type?: string;
  osm_id?: number;
  extratags?: Record<string, string>;
  address?: Record<string, string>;
};

/** Forward geocode / place search via Nominatim. */
export async function searchNominatim(
  query: string,
  limit = 12,
): Promise<SearchResult[]> {
  const q = String(query || "").trim();
  if (!q) {
    return [];
  }
  try {
    const url = new URL("https://nominatim.openstreetmap.org/search");
    url.searchParams.set("q", q);
    url.searchParams.set("format", "jsonv2");
    url.searchParams.set("addressdetails", "1");
    url.searchParams.set("extratags", "1");
    url.searchParams.set("limit", String(Math.min(40, Math.max(1, limit))));

    const upstream = await fetch(url.toString(), {
      redirect: "follow",
      signal: AbortSignal.timeout(5000),
      headers: {
        Accept: "application/json",
        "User-Agent": NOMINATIM_UA,
      },
    });
    if (!upstream.ok) {
      return [];
    }
    const rows = (await upstream.json()) as NominatimSearchHit[];
    if (!Array.isArray(rows)) {
      return [];
    }

    const out: SearchResult[] = [];
    for (const row of rows) {
      const lat = parseCoord(row.lat);
      const lon = parseCoord(row.lon);
      if (!Number.isFinite(lat) || !Number.isFinite(lon)) {
        continue;
      }
      const tags = row.extratags || {};
      const osmType = String(row.osm_type || "node").toLowerCase();
      const osmId = row.osm_id;
      const urlOsm =
        osmId != null
          ? `https://www.openstreetmap.org/${osmType}/${osmId}`
          : `https://www.openstreetmap.org/?mlat=${lat}&mlon=${lon}#map=18/${lat}/${lon}`;
      const name =
        String(row.name || "").trim() ||
        String(row.display_name || "")
          .split(",")[0]
          ?.trim() ||
        "Place";
      const address = shortAddressFromParts(
        row.address,
        String(row.display_name || "").trim(),
      );
      out.push({
        title: name,
        url: urlOsm,
        snippet: address,
        category: "map",
        source: "nominatim",
        lat,
        lon,
        address,
        ...(tags.opening_hours
          ? { openingHours: String(tags.opening_hours).trim() }
          : {}),
        ...(tags.phone || tags["contact:phone"]
          ? {
              phone: String(tags.phone || tags["contact:phone"] || "").trim(),
            }
          : {}),
        ...(tags.website || tags["contact:website"]
          ? {
              website: String(
                tags.website || tags["contact:website"] || "",
              ).trim(),
            }
          : {}),
      });
      if (out.length >= limit) {
        break;
      }
    }
    return out;
  } catch {
    return [];
  }
}

type PhotonFeature = {
  geometry?: { coordinates?: number[] };
  properties?: {
    name?: string;
    street?: string;
    housenumber?: string;
    city?: string;
    town?: string;
    village?: string;
    municipality?: string;
    country?: string;
    state?: string;
    osm_key?: string;
    osm_value?: string;
    osm_type?: string;
    osm_id?: number;
    type?: string;
  };
};

function photonAddress(props: NonNullable<PhotonFeature["properties"]>): string {
  const place =
    props.city || props.town || props.village || props.municipality || "";
  const street = [props.street, props.housenumber].filter(Boolean).join(" ");
  return [street, place, props.state, props.country]
    .map((v) => String(v || "").trim())
    .filter(Boolean)
    .join(", ");
}

/** Place search via Photon (Komoot) — denser map lists than Nominatim alone. */
export async function searchPhoton(
  query: string,
  limit = 40,
  bias?: { lat: number; lon: number },
): Promise<SearchResult[]> {
  const q = String(query || "").trim();
  if (!q) {
    return [];
  }
  try {
    const url = new URL("https://photon.komoot.io/api/");
    url.searchParams.set("q", q);
    url.searchParams.set("limit", String(Math.min(100, Math.max(1, limit))));
    url.searchParams.set("lang", "de");
    if (
      bias &&
      Number.isFinite(bias.lat) &&
      Number.isFinite(bias.lon)
    ) {
      url.searchParams.set("lat", String(bias.lat));
      url.searchParams.set("lon", String(bias.lon));
    }

    const upstream = await fetch(url.toString(), {
      redirect: "follow",
      signal: AbortSignal.timeout(5000),
      headers: {
        Accept: "application/json",
        "User-Agent": NOMINATIM_UA,
      },
    });
    if (!upstream.ok) {
      return [];
    }
    const data = (await upstream.json()) as { features?: PhotonFeature[] };
    const features = Array.isArray(data.features) ? data.features : [];
    const out: SearchResult[] = [];
    for (const feat of features) {
      const coords = feat.geometry?.coordinates;
      if (!Array.isArray(coords) || coords.length < 2) {
        continue;
      }
      const lon = Number(coords[0]);
      const lat = Number(coords[1]);
      if (!Number.isFinite(lat) || !Number.isFinite(lon)) {
        continue;
      }
      const props = feat.properties || {};
      const osmTypeRaw = String(props.osm_type || "N").toUpperCase();
      const osmType =
        osmTypeRaw === "W" || osmTypeRaw === "WAY"
          ? "way"
          : osmTypeRaw === "R" || osmTypeRaw === "RELATION"
            ? "relation"
            : "node";
      const osmId = props.osm_id;
      const urlOsm =
        osmId != null
          ? `https://www.openstreetmap.org/${osmType}/${osmId}`
          : `https://www.openstreetmap.org/?mlat=${lat}&mlon=${lon}#map=16/${lat}/${lon}`;
      const title = String(props.name || "").trim() || "Place";
      const address = photonAddress(props);
      const osmKey = String(props.osm_key || "").trim();
      const osmValue = String(props.osm_value || props.type || "").trim();
      const placeType =
        osmKey && osmValue ? `${osmKey}=${osmValue}` : osmValue || undefined;
      out.push({
        title,
        url: urlOsm,
        snippet: address,
        category: "map",
        source: "photon",
        lat,
        lon,
        address,
        osmType,
        ...(osmId != null ? { osmId: Number(osmId) } : {}),
        ...(placeType ? { placeType } : {}),
      });
      if (out.length >= limit) {
        break;
      }
    }
    return out;
  } catch {
    return [];
  }
}

/**
 * Dense map results: full-query Photon + brand search biased to the first hit.
 * Address queries also load nearby POIs (restaurants, shops, fuel, doctors…).
 */
export async function searchPlacesDense(
  query: string,
  limit = 40,
): Promise<SearchResult[]> {
  const q = String(query || "").trim();
  if (!q) {
    return [];
  }
  const tokens = q.split(/\s+/).filter(Boolean);
  const primary = await searchPhoton(q, Math.min(12, limit));
  const seen = new Set(
    primary.map(
      (r) => `${r.lat!.toFixed(5)},${r.lon!.toFixed(5)}|${r.title.toLowerCase()}`,
    ),
  );
  let out = [...primary];

  if (looksLikeAddressQuery(q, primary) && primary[0]?.lat != null && primary[0]?.lon != null) {
    const cityHint =
      extractCityHint(q, primary[0].address || primary[0].snippet || "") ||
      "";
    const nearby = await searchNearbyPois(
      primary[0].lat!,
      primary[0].lon!,
      Math.max(limit - 1, 20),
      cityHint,
    );
    // Keep the address / place first, then nearby businesses.
    const addressHit: SearchResult = {
      ...primary[0],
      featured: true,
      title:
        primary[0].title && primary[0].title !== "Place"
          ? primary[0].title
          : q.split(",")[0]?.trim() || primary[0].title,
    };
    const out: SearchResult[] = [addressHit];
    const seen = new Set([
      `${addressHit.lat!.toFixed(5)},${addressHit.lon!.toFixed(5)}|${addressHit.title.toLowerCase()}`,
    ]);
    for (const hit of nearby) {
      // Skip pure street / place duplicates.
      const pt = String(hit.placeType || "").toLowerCase();
      if (
        /^(place|highway|building)=/.test(pt) ||
        /^(street|city|town|village|house)$/i.test(pt)
      ) {
        continue;
      }
      const key = `${hit.lat!.toFixed(5)},${hit.lon!.toFixed(5)}|${hit.title.toLowerCase()}`;
      if (seen.has(key)) {
        continue;
      }
      seen.add(key);
      out.push(hit);
      if (out.length >= limit) {
        break;
      }
    }
    return out.slice(0, limit);
  }

  const biasFrom = primary[0];
  const brand = tokens[0];
  if (biasFrom && brand && tokens.length >= 2 && out.length < limit) {
    const nearby = await searchPhoton(brand, limit, {
      lat: biasFrom.lat!,
      lon: biasFrom.lon!,
    });
    for (const hit of nearby) {
      const key = `${hit.lat!.toFixed(5)},${hit.lon!.toFixed(5)}|${hit.title.toLowerCase()}`;
      if (seen.has(key)) {
        continue;
      }
      seen.add(key);
      out.push(hit);
      if (out.length >= limit) {
        break;
      }
    }
  }

  // Still thin? try brand-only Photon for national chains.
  if (out.length < Math.min(12, limit) && brand && brand.length >= 2) {
    const broad = await searchPhoton(brand, limit);
    for (const hit of broad) {
      const key = `${hit.lat!.toFixed(5)},${hit.lon!.toFixed(5)}|${hit.title.toLowerCase()}`;
      if (seen.has(key)) {
        continue;
      }
      seen.add(key);
      out.push(hit);
      if (out.length >= limit) {
        break;
      }
    }
  }

  return out.slice(0, limit);
}

const CHAIN_HINT =
  /^(penny|edeka|aldi|lidl|rewe|netto|kaufland|dm|rossmann|mcdonald|burger|starbucks|shell|aral|total|ikea|mediamarkt|saturn|7-eleven|seven\s*eleven)/i;

function extractCityHint(query: string, address: string): string {
  const blob = `${query}, ${address}`;
  const m = blob.match(
    /\b(\d{5})\s+([A-ZÄÖÜ][a-zäöüß\-]+(?:\s+[A-ZÄÖÜ][a-zäöüß\-]+)?)/,
  );
  if (m?.[2]) {
    return m[2].trim();
  }
  const known = blob.match(
    /\b(Osnabrück|Berlin|Hamburg|München|Munich|Köln|Cologne|Frankfurt|Dortmund|Essen|Stuttgart|Düsseldorf|Bremen|Hannover|Leipzig|Dresden|Nürnberg|Bochum|Wuppertal|Bielefeld|Bonn|Münster|Karlsruhe|Mannheim|Augsburg|Wiesbaden|Velbert|Lotte|Bramsche)\b/i,
  );
  if (known?.[1]) {
    return known[1];
  }
  const parts = query.split(/[,\s]+/).filter(Boolean);
  const last = parts[parts.length - 1] || "";
  if (last.length >= 3 && !/^\d/.test(last) && !/straße|strasse|str\./i.test(last)) {
    return last;
  }
  return "";
}

function looksLikeAddressQuery(query: string, hits: SearchResult[]): boolean {
  const q = query.trim();
  if (CHAIN_HINT.test(q.split(/\s+/)[0] || "")) {
    return false;
  }
  // House number + street-ish token, or DE postcode, or comma address.
  if (
    /\d{1,4}[a-z]?/i.test(q) &&
    /(straße|strasse|str\.|weg|platz|allee|gasse|ring|damm|ufer|chaussee|road|street|ave|lane|way)\b/i.test(
      q,
    )
  ) {
    return true;
  }
  if (/\b\d{5}\b/.test(q) && /[a-zäöüß]/i.test(q)) {
    return true;
  }
  if (/,/.test(q) && /\d/.test(q)) {
    return true;
  }
  const first = hits[0];
  const pt = String(first?.placeType || "").toLowerCase();
  if (
    /^(place|building|highway|boundary|landuse)=/.test(pt) ||
    pt.includes("house") ||
    pt.includes("residential") ||
    pt.includes("building")
  ) {
    return true;
  }
  // Photon often returns type "house" / "street" without osm_key=shop.
  const osmValue = pt.includes("=") ? pt.split("=")[1] : pt;
  if (
    /^(house|street|city|town|village|suburb|neighbourhood|district|postcode|hamlet)$/i.test(
      osmValue || "",
    )
  ) {
    return true;
  }
  return false;
}

function haversineM(
  lat1: number,
  lon1: number,
  lat2: number,
  lon2: number,
): number {
  const toRad = (d: number) => (d * Math.PI) / 180;
  const R = 6371000;
  const dLat = toRad(lat2 - lat1);
  const dLon = toRad(lon2 - lon1);
  const a =
    Math.sin(dLat / 2) ** 2 +
    Math.cos(toRad(lat1)) * Math.cos(toRad(lat2)) * Math.sin(dLon / 2) ** 2;
  return 2 * R * Math.asin(Math.sqrt(a));
}

/** Nearby businesses around an address — Google/Brave-style local explore. */
export async function searchNearbyPois(
  lat: number,
  lon: number,
  limit = 40,
  cityHint = "",
): Promise<SearchResult[]> {
  if (!Number.isFinite(lat) || !Number.isFinite(lon)) {
    return [];
  }
  const radius = 2500;
  const bias = { lat, lon };
  const city = String(cityHint || "").trim();

  type Ranked = SearchResult & { _dist: number; _score: number };
  const ranked: Ranked[] = [];

  const pushHit = (
    hit: SearchResult,
    source: string,
    scoreBoost = 0,
  ) => {
    if (hit.lat == null || hit.lon == null) {
      return;
    }
    const dist = haversineM(lat, lon, hit.lat, hit.lon);
    if (dist > 8000) {
      return;
    }
    const pt = String(hit.placeType || "").toLowerCase();
    if (
      pt.includes("amenity=parking") ||
      pt.includes("highway=") ||
      /^(street|house|city|town|village|postcode)$/i.test(pt)
    ) {
      return;
    }
    ranked.push({
      ...hit,
      source,
      address: hit.address || `${Math.round(dist)} m away`,
      snippet: hit.snippet || `${Math.round(dist)} m away`,
      _dist: dist,
      _score: scoreBoost + Math.max(0, 50 - dist / 80),
    });
  };

  // Prefer a few high-signal Photon queries with location bias (Workers-friendly).
  const photonTerms = city
    ? [
        `Restaurant ${city}`,
        `Café ${city}`,
        `Supermarkt ${city}`,
        `Tankstelle ${city}`,
        `Apotheke ${city}`,
        `Arzt ${city}`,
        `Bäckerei ${city}`,
        `Penny ${city}`,
        `Edeka ${city}`,
      ]
    : [
        "Restaurant",
        "Café",
        "Supermarkt",
        "Tankstelle",
        "Apotheke",
        "Arzt",
        "Bäckerei",
        "Hotel",
      ];

  const overpassPromise = overpassQuery(
    `[out:json][timeout:12];
(
  nwr(around:${radius},${lat},${lon})["amenity"~"restaurant|cafe|fast_food|bar|pub|pharmacy|doctors|clinic|dentist|hospital|fuel|bank|atm"];
  nwr(around:${radius},${lat},${lon})["shop"~"supermarket|convenience|bakery|chemist|kiosk"];
  nwr(around:${radius},${lat},${lon})["tourism"~"hotel|guest_house"];
);
out center tags 80;`,
  );

  const photonPromise = (async () => {
    const out: SearchResult[] = [];
    // Sequential small batches — more reliable than a burst of parallel Photons.
    for (let i = 0; i < photonTerms.length; i += 2) {
      const chunk = photonTerms.slice(i, i + 2);
      try {
        const part = await Promise.all(
          chunk.map((term) => searchPhoton(term, 15, bias)),
        );
        for (const batch of part) {
          out.push(...batch);
        }
      } catch {
        // continue
      }
    }
    return out;
  })();

  const [elements, photonHits] = await Promise.all([
    overpassPromise,
    photonPromise,
  ]);

  for (const hit of photonHits) {
    pushHit(hit, "photon-nearby", 18);
  }

  for (const el of elements) {
    const tags = el.tags || {};
    if (tags.amenity === "parking" || tags.parking) {
      continue;
    }
    const elLat = Number(el.lat ?? el.center?.lat);
    const elLon = Number(el.lon ?? el.center?.lon);
    if (!Number.isFinite(elLat) || !Number.isFinite(elLon)) {
      continue;
    }
    const name = String(tags.name || tags.brand || "").trim();
    if (!name) {
      continue;
    }
    const fromTags = tagsFromOsm(tags);
    const dist = haversineM(lat, lon, elLat, elLon);
    let score = 22;
    if (tags.amenity === "restaurant" || tags.amenity === "cafe" || tags.amenity === "fast_food") {
      score += 30;
    } else if (tags.shop === "supermarket" || tags.shop === "convenience") {
      score += 28;
    } else if (tags.amenity === "fuel" || tags.amenity === "pharmacy") {
      score += 26;
    } else if (
      tags.amenity === "doctors" ||
      tags.amenity === "clinic" ||
      tags.amenity === "dentist" ||
      tags.amenity === "hospital"
    ) {
      score += 24;
    } else if (tags.shop || tags.amenity) {
      score += 16;
    }
    if (tags.opening_hours) {
      score += 8;
    }
    const osmType = String(el.type || "node").toLowerCase();
    const osmId = el.id;
    ranked.push({
      title: name,
      url:
        osmId != null
          ? `https://www.openstreetmap.org/${osmType}/${osmId}`
          : `https://www.openstreetmap.org/?mlat=${elLat}&mlon=${elLon}#map=17/${elLat}/${elLon}`,
      snippet: fromTags.placeType || `${Math.round(dist)} m away`,
      category: "map",
      source: "overpass-nearby",
      lat: elLat,
      lon: elLon,
      address: `${Math.round(dist)} m away`,
      osmType,
      ...(osmId != null ? { osmId: Number(osmId) } : {}),
      ...(fromTags.placeType ? { placeType: fromTags.placeType } : {}),
      ...(fromTags.openingHours ? { openingHours: fromTags.openingHours } : {}),
      ...(fromTags.phone ? { phone: fromTags.phone } : {}),
      ...(fromTags.website ? { website: fromTags.website } : {}),
      _dist: dist,
      _score: score + Math.max(0, 40 - dist / 25),
    });
  }

  ranked.sort((a, b) => b._score - a._score || a._dist - b._dist);

  const typeCount = new Map<string, number>();
  const seen = new Set<string>();
  const out: SearchResult[] = [];
  for (const item of ranked) {
    const dedupe = `${item.lat!.toFixed(5)},${item.lon!.toFixed(5)}|${item.title.toLowerCase()}`;
    if (seen.has(dedupe)) {
      continue;
    }
    seen.add(dedupe);
    const key = String(item.placeType || "other");
    const n = typeCount.get(key) || 0;
    if (n >= Math.max(6, Math.ceil(limit / 4))) {
      continue;
    }
    typeCount.set(key, n + 1);
    const { _dist, _score, ...rest } = item;
    out.push(rest);
    if (out.length >= limit) {
      break;
    }
  }
  return out;
}

function tagsFromOsm(
  tags: Record<string, string> | undefined,
): Pick<GeocodePlace, "openingHours" | "phone" | "website" | "wikipedia" | "placeType" | "name"> {
  const t = tags || {};
  const placeType =
    t.shop
      ? `shop=${t.shop}`
      : t.amenity
        ? `amenity=${t.amenity}`
        : t.tourism
          ? `tourism=${t.tourism}`
          : t.leisure
            ? `leisure=${t.leisure}`
            : t.office
              ? `office=${t.office}`
              : undefined;
  return {
    name: String(t.name || t.brand || "").trim(),
    openingHours: String(
      t.opening_hours || t["opening_hours:covid19"] || "",
    ).trim(),
    phone: String(t.phone || t["contact:phone"] || "").trim(),
    website: String(t.website || t["contact:website"] || "").trim(),
    wikipedia: String(t.wikipedia || "").trim(),
    placeType,
  };
}

async function overpassQuery(query: string): Promise<
  Array<{
    type?: string;
    id?: number;
    lat?: number;
    lon?: number;
    center?: { lat?: number; lon?: number };
    tags?: Record<string, string>;
  }>
> {
  const endpoints = [
    "https://overpass-api.de/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
  ];
  for (const endpoint of endpoints) {
    try {
      const upstream = await fetch(endpoint, {
        method: "POST",
        redirect: "follow",
        signal: AbortSignal.timeout(9000),
        headers: {
          "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
          "User-Agent": NOMINATIM_UA,
          Accept: "application/json",
        },
        body: `data=${encodeURIComponent(query)}`,
      });
      if (!upstream.ok) {
        continue;
      }
      const ctype = upstream.headers.get("content-type") || "";
      if (!ctype.includes("json")) {
        continue;
      }
      const data = (await upstream.json()) as {
        elements?: Array<{
          type?: string;
          id?: number;
          lat?: number;
          lon?: number;
          center?: { lat?: number; lon?: number };
          tags?: Record<string, string>;
        }>;
      };
      return Array.isArray(data.elements) ? data.elements : [];
    } catch {
      // try next endpoint
    }
  }
  return [];
}

function scoreOsmElement(
  el: { tags?: Record<string, string> },
  wantName: string,
): number {
  const t = el.tags || {};
  let score = 0;
  if (t.opening_hours) {
    score += 50;
  }
  if (t.shop === "supermarket" || t.shop === "convenience") {
    score += 40;
  } else if (t.shop) {
    score += 25;
  } else if (t.amenity === "cafe" || t.amenity === "restaurant" || t.amenity === "fast_food") {
    score += 30;
  } else if (t.amenity === "parking" || t.building) {
    score -= 20;
  }
  const name = String(t.name || "").toLowerCase();
  const brand = String(t.brand || "").toLowerCase();
  const want = wantName.toLowerCase();
  if (want && (name.includes(want) || brand.includes(want))) {
    score += 20;
  }
  if (t.phone || t["contact:phone"]) {
    score += 5;
  }
  if (t.website || t["contact:website"]) {
    score += 5;
  }
  return score;
}

/** Fetch OSM tags (opening_hours, phone, …) by element id. */
export async function fetchOsmElement(
  osmType: string,
  osmId: number,
): Promise<GeocodePlace> {
  const empty: GeocodePlace = {
    address: "",
    name: "",
    openingHours: "",
    phone: "",
    website: "",
    wikipedia: "",
  };
  const type = String(osmType || "").toLowerCase();
  if (!["node", "way", "relation"].includes(type) || !Number.isFinite(osmId)) {
    return empty;
  }
  const elements = await overpassQuery(
    `[out:json][timeout:8];${type}(${Math.floor(osmId)});out tags center;`,
  );
  const el = elements[0];
  if (!el?.tags) {
    return empty;
  }
  const fromTags = tagsFromOsm(el.tags);
  return {
    ...empty,
    ...fromTags,
    address: "",
  };
}

/**
 * Enrich a map place: OSM id first, then nearby Overpass match by name,
 * then Nominatim reverse as address fallback.
 */
export async function enrichMapPlace(opts: {
  lat: number;
  lon: number;
  name?: string;
  osmType?: string;
  osmId?: number;
}): Promise<GeocodePlace> {
  const empty: GeocodePlace = {
    address: "",
    name: "",
    openingHours: "",
    phone: "",
    website: "",
    wikipedia: "",
  };

  let fromOsm: GeocodePlace = empty;
  if (opts.osmType && opts.osmId != null) {
    fromOsm = await fetchOsmElement(opts.osmType, opts.osmId);
  }

  const isWeakPoi =
    !fromOsm.openingHours ||
    /parking|building=/.test(String(fromOsm.placeType || ""));

  // Nearby shops often have opening_hours on the POI even when the Photon/OSM
  // id points at a parking lot / building named after the store (common for Penny).
  if (isWeakPoi && Number.isFinite(opts.lat) && Number.isFinite(opts.lon)) {
    const name = String(opts.name || fromOsm.name || "").trim();
    const safeName = name.replace(/[\\"']/g, "").slice(0, 64);
    const nameFilters = safeName
      ? `
  nwr(around:120,${opts.lat},${opts.lon})["shop"]["name"~"${safeName}",i];
  nwr(around:120,${opts.lat},${opts.lon})["shop"]["brand"~"${safeName}",i];
  nwr(around:120,${opts.lat},${opts.lon})["opening_hours"]["name"~"${safeName}",i];
  nwr(around:120,${opts.lat},${opts.lon})["opening_hours"]["brand"~"${safeName}",i];
  nwr(around:120,${opts.lat},${opts.lon})["amenity"~"cafe|restaurant|fast_food|pharmacy|bank|fuel"]["name"~"${safeName}",i];`
      : `
  nwr(around:80,${opts.lat},${opts.lon})["opening_hours"]["shop"];
  nwr(around:80,${opts.lat},${opts.lon})["opening_hours"]["amenity"];`;
    const around = await overpassQuery(
      `[out:json][timeout:8];(${nameFilters});out tags center 16;`,
    );
    const ranked = [...around].sort(
      (a, b) => scoreOsmElement(b, safeName) - scoreOsmElement(a, safeName),
    );
    const best = ranked[0] || null;
    if (best?.tags && scoreOsmElement(best, safeName) > 0) {
      const tags = tagsFromOsm(best.tags);
      fromOsm = {
        ...fromOsm,
        openingHours: tags.openingHours || fromOsm.openingHours,
        phone: tags.phone || fromOsm.phone,
        website: tags.website || fromOsm.website,
        wikipedia: tags.wikipedia || fromOsm.wikipedia,
        name: tags.name || fromOsm.name,
        placeType: tags.placeType || fromOsm.placeType,
      };
    }
  }

  const reverse = await reverseGeocode(opts.lat, opts.lon);
  return {
    address: reverse.address || fromOsm.address || "",
    name: fromOsm.name || reverse.name || String(opts.name || "").trim(),
    openingHours: fromOsm.openingHours || reverse.openingHours || "",
    phone: fromOsm.phone || reverse.phone || "",
    website: fromOsm.website || reverse.website || "",
    wikipedia: fromOsm.wikipedia || reverse.wikipedia || "",
    placeType: fromOsm.placeType || reverse.placeType,
  };
}
