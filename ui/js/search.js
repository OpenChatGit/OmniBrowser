(() => {
  const params = new URLSearchParams(window.location.search);

  /** Maps is off by default. Enable with localStorage omni.features.maps=1 or ?maps=1 */
  function mapsFeatureEnabled() {
    try {
      if (params.get("maps") === "1" || params.get("enableMaps") === "1") {
        localStorage.setItem("omni.features.maps", "1");
        return true;
      }
      if (params.get("maps") === "0" || params.get("enableMaps") === "0") {
        localStorage.removeItem("omni.features.maps");
        return false;
      }
      return localStorage.getItem("omni.features.maps") === "1";
    } catch (_) {
      return false;
    }
  }

  const MAPS_ENABLED = mapsFeatureEnabled();

  let query = String(params.get("q") || "").trim();
  let pageParam = Math.max(1, Number(params.get("page") || "1") || 1);
  const rawCat = String(params.get("tab") || params.get("category") || "general")
    .toLowerCase();
  let categoryParam =
    rawCat === "all" || rawCat === "web" || !rawCat
      ? "general"
      : rawCat === "maps"
        ? "map"
        : rawCat;
  if (!MAPS_ENABLED && categoryParam === "map") {
    categoryParam = "general";
  }

  function normalizeCategory(category) {
    const cat = category || "general";
    if (!MAPS_ENABLED && cat === "map") {
      return "general";
    }
    return cat;
  }

  const PAGE_SIZE = () =>
    categoryParam === "images" || categoryParam === "videos"
      ? 24
      : categoryParam === "map"
        ? mapResultLimit()
        : 18;

  function mapResultLimit() {
    try {
      const n = Number(localStorage.getItem("omni.maps.resultLimit"));
      if (Number.isFinite(n)) {
        return Math.min(100, Math.max(10, Math.round(n)));
      }
    } catch (_) {
      // ignore
    }
    return 40;
  }

  window.qubrainMapResultLimit = mapResultLimit;
  window.qubrainMapsEnabled = () => MAPS_ENABLED;
  let searchEpoch = 0;
  let searchAbort = null;
  let lastPaintKey = "";
  const resultMemCache = new Map();
  const previewMemCache = new Map();
  const faviconHostCache = new Map();
  let previewActive = 0;
  const previewWaiters = [];
  const PREVIEW_CONCURRENCY = 3;
  let mapsStackPromise = null;

  function loadStylesheet(href) {
    return new Promise((resolve, reject) => {
      if (document.querySelector(`link[data-omni-href="${href}"]`)) {
        resolve();
        return;
      }
      const link = document.createElement("link");
      link.rel = "stylesheet";
      link.href = href;
      link.dataset.omniHref = href;
      link.onload = () => resolve();
      link.onerror = () => reject(new Error(`Failed to load ${href}`));
      document.head.appendChild(link);
    });
  }

  function loadScript(src) {
    return new Promise((resolve, reject) => {
      if (document.querySelector(`script[data-omni-src="${src}"]`)) {
        resolve();
        return;
      }
      const script = document.createElement("script");
      script.src = src;
      script.async = false;
      script.dataset.omniSrc = src;
      script.onload = () => resolve();
      script.onerror = () => reject(new Error(`Failed to load ${src}`));
      document.body.appendChild(script);
    });
  }

  /** Leaflet / maps stack only when maps feature is on. */
  function ensureMapsStack() {
    if (!MAPS_ENABLED) {
      return Promise.resolve();
    }
    if (window.QuBrainMaps) {
      return Promise.resolve();
    }
    if (!mapsStackPromise) {
      mapsStackPromise = (async () => {
        await loadStylesheet("css/permission-popup.css");
        await loadStylesheet("vendor/leaflet/leaflet.css");
        await loadScript("vendor/leaflet/leaflet.js");
        await loadScript("js/lib/permission-popup.js");
        await loadScript("js/search-maps.js");
      })().catch((err) => {
        console.error("maps stack failed to load", err);
        mapsStackPromise = null;
      });
    }
    return mapsStackPromise || Promise.resolve();
  }

  const form = document.getElementById("serp-search");
  const input = document.getElementById("serp-q");
  const resultsEl = document.getElementById("serp-results");
  const emptyEl = document.getElementById("serp-empty");
  const sideEl = document.getElementById("serp-side");
  const pagerEl = document.getElementById("serp-pager");
  const nextBtn = document.getElementById("serp-next");
  const prevBtn = document.getElementById("serp-prev");
  const tabsEl = document.getElementById("serp-tabs");

  const wikiLang = String(navigator.language || "en")
    .toLowerCase()
    .startsWith("de")
    ? "de"
    : "en";

  const LABELS = {
    de: {
      instanceOf: "Art",
      legalForm: "Rechtsform",
      inception: "Gründung",
      website: "Website",
      headquarters: "Sitz",
      industry: "Branche",
      moreAbout: "Mehr über",
      dataFrom: "Daten von Wikipedia",
      dataFromSearx: "Infobox von SearXNG",
      loading: "Lade Infos…",
      none: "Kein Wikipedia-Eintrag gefunden.",
      nextPage: "Nächste Seite",
      prevPage: "Vorherige Seite",
      loadingMore: "Lade…",
      tabAll: "Alle",
      tabImages: "Bilder",
      tabNews: "News",
      tabVideos: "Videos",
      tabMaps: "Karten",
    },
    en: {
      instanceOf: "Type",
      legalForm: "Legal form",
      inception: "Founded",
      website: "Website",
      headquarters: "Headquarters",
      industry: "Industry",
      moreAbout: "More about",
      dataFrom: "Data from Wikipedia",
      dataFromSearx: "Infobox from SearXNG",
      loading: "Loading info…",
      none: "No Wikipedia article found.",
      nextPage: "Next page",
      prevPage: "Previous",
      loadingMore: "Loading…",
      tabAll: "All",
      tabImages: "Images",
      tabNews: "News",
      tabVideos: "Videos",
      tabMaps: "Maps",
    },
  }[wikiLang];

  if (nextBtn) {
    nextBtn.textContent = LABELS.nextPage;
  }
  if (prevBtn) {
    prevBtn.textContent = LABELS.prevPage;
  }

  if (tabsEl) {
    const labels = {
      general: LABELS.tabAll,
      images: LABELS.tabImages,
      news: LABELS.tabNews,
      videos: LABELS.tabVideos,
      map: LABELS.tabMaps,
    };
    if (MAPS_ENABLED && !tabsEl.querySelector('.serp-tab[data-cat="map"]')) {
      const mapBtn = document.createElement("button");
      mapBtn.type = "button";
      mapBtn.className = "serp-tab";
      mapBtn.setAttribute("data-cat", "map");
      mapBtn.textContent = LABELS.tabMaps;
      tabsEl.appendChild(mapBtn);
    }
    tabsEl.querySelectorAll(".serp-tab").forEach((btn) => {
      const cat = btn.getAttribute("data-cat") || "general";
      if (labels[cat]) {
        btn.textContent = labels[cat];
      }
      btn.classList.toggle("is-active", cat === categoryParam);
    });
  }

  const FACT_PROPS = [
    { id: "P1454", label: LABELS.legalForm },
    { id: "P31", label: LABELS.instanceOf },
    { id: "P571", label: LABELS.inception },
    { id: "P159", label: LABELS.headquarters },
    { id: "P452", label: LABELS.industry },
  ];

  // Prefer companies / orgs over products, websites, people, etc.
  const ORG_TYPE_IDS = new Set([
    "Q4830453", // business
    "Q783794", // company
    "Q891723", // public company
    "Q167037", // corporation
    "Q6881511", // enterprise
    "Q43229", // organization
    "Q18388277", // technology company
    "Q1058914", // software company
    "Q1331793", // media company
    "Q22687", // bank
    "Q134161", // joint-stock company
    "Q1589009", // publicly held company
    "Q166280", // limited liability company
    "Q422074", // GmbH
  ]);

  const SITE_KEY = wikiLang === "de" ? "dewiki" : "enwiki";

  function hostLabel(url) {
    try {
      return new URL(url).hostname.replace(/^www\./, "");
    } catch (_) {
      return url;
    }
  }

  function siteDisplayName(url, fallback) {
    if (fallback) {
      return fallback;
    }
    const host = hostLabel(url);
    const labels = host.split(".").filter(Boolean);
    if (labels.length >= 2) {
      const brand = labels[labels.length - 2];
      if (brand && brand.length > 2) {
        return brand.charAt(0).toUpperCase() + brand.slice(1);
      }
    }
    const base = labels[0] || host;
    if (!base) {
      return host;
    }
    return base.charAt(0).toUpperCase() + base.slice(1);
  }

  function faviconUrlFor(url) {
    try {
      const bare = new URL(url).hostname.replace(/^www\./, "");
      if (faviconHostCache.has(bare)) {
        return faviconHostCache.get(bare);
      }
      const src = `https://icons.duckduckgo.com/ip3/${bare}.ico`;
      faviconHostCache.set(bare, src);
      return src;
    } catch (_) {
      return "assets/qubrain.svg";
    }
  }

  function bindFavicon(img, url) {
    img.decoding = "async";
    img.loading = "lazy";
    img.referrerPolicy = "no-referrer";
    img.alt = "";
    img.onerror = () => {
      img.onerror = null;
      img.src = "assets/qubrain.svg";
    };
    img.src = faviconUrlFor(url);
  }

  function truncateSnippet(text, maxLen = 155) {
    const clean = String(text || "")
      .replace(/\s+/g, " ")
      .trim();
    if (!clean) {
      return "";
    }
    if (clean.length <= maxLen) {
      return clean;
    }
    let cut = clean.slice(0, Math.max(0, maxLen - 4));
    const lastSpace = cut.lastIndexOf(" ");
    if (lastSpace > maxLen * 0.55) {
      cut = cut.slice(0, lastSpace);
    }
    return `${cut.trim()}....`;
  }

  function mapApiResults(apiResults) {
    if (!Array.isArray(apiResults)) {
      return [];
    }
    return apiResults
      .filter((item) => {
        if (!item || !item.title) {
          return false;
        }
        // Map hits may lack a canonical URL but still have coordinates.
        if (item.category === "map" || item.lat != null || item.lon != null) {
          return true;
        }
        return Boolean(item.url);
      })
      .map((item) => {
        const thumbs = Array.isArray(item.thumbnails)
          ? item.thumbnails.filter(Boolean)
          : [];
        const thumbnail = item.thumbnail || item.image || thumbs[0] || "";
        const image = item.image || item.thumbnail || "";
        const candidates = [...thumbs, thumbnail, image].filter(Boolean);
        return {
          name: siteDisplayName(item.url),
          title: item.title,
          url: item.url,
          displayUrl: hostLabel(item.url),
          snippet: item.snippet || "",
          featured: Boolean(item.featured),
          category: item.category || "",
          thumbnail,
          image,
          thumbnails: [...new Set(candidates)],
          source: item.source || "",
          published: item.published || "",
          lat: parseMapCoord(item.lat),
          lon: parseMapCoord(item.lon),
          address: item.address || "",
          openingHours: item.openingHours || item.opening_hours || "",
          phone: item.phone || "",
          website: item.website || "",
          osmType: item.osmType || "",
          osmId:
            item.osmId == null || item.osmId === ""
              ? null
              : Number(item.osmId),
          placeType: item.placeType || "",
        };
      });
  }

  function parseMapCoord(raw) {
    if (raw == null || raw === "") {
      return null;
    }
    if (typeof raw === "number") {
      return Number.isFinite(raw) ? raw : null;
    }
    const n = Number(String(raw).trim().replace(",", "."));
    return Number.isFinite(n) ? n : null;
  }

  function imageProxyUrl(src) {
    return `${searchApiBase()}/v1/img?u=${encodeURIComponent(src)}`;
  }

  function prefersImageProxy(src) {
    try {
      const host = new URL(src).hostname.replace(/^www\./, "").toLowerCase();
      return (
        host.endsWith("mm.bing.net") ||
        host.endsWith("bing.net") ||
        host.endsWith("gstatic.com") ||
        host.endsWith("googleusercontent.com") ||
        host.endsWith("ggpht.com") ||
        host.endsWith("pinimg.com") ||
        host.endsWith("twimg.com")
      );
    } catch (_) {
      return false;
    }
  }

  function expandImageSources(candidates) {
    const direct = [...new Set((candidates || []).filter(Boolean))];
    const sources = [];
    for (const src of direct) {
      if (prefersImageProxy(src)) {
        sources.push(imageProxyUrl(src), src);
      } else {
        sources.push(src, imageProxyUrl(src));
      }
      // Cap fan-out: first candidate gets proxy+direct at most.
      if (sources.length >= 2) {
        break;
      }
    }
    return sources;
  }

  function bindResultImage(img, link, candidates, onSettled) {
    const sources = expandImageSources(candidates);
    let index = 0;
    let settled = false;

    const finish = (ok) => {
      if (settled) {
        return;
      }
      settled = true;
      img.dataset.settled = "1";
      img.dataset.loaded = ok ? "1" : "0";
      if (ok && link) {
        link.classList.add("is-ready");
        link.classList.remove("is-failed");
      }
      img.dispatchEvent(
        new CustomEvent("qubrain-img-settled", {
          detail: { ok },
          bubbles: true,
        }),
      );
      if (!ok && link && link.classList.contains("serp-image")) {
        link.remove();
      }
      if (typeof onSettled === "function") {
        onSettled(ok);
      }
    };

    const show = (src) => {
      img.referrerPolicy = "no-referrer";
      img.decoding = "async";
      img.src = src;
    };

    img.onload = () => {
      if (img.naturalWidth < 2 || img.naturalHeight < 2) {
        img.onerror();
        return;
      }
      finish(true);
    };

    img.onerror = () => {
      index += 1;
      if (index < sources.length) {
        show(sources[index]);
        return;
      }
      finish(false);
    };

    if (sources.length) {
      show(sources[0]);
    } else {
      finish(false);
    }
  }

  let imageFeed = {
    page: 1,
    hasMore: false,
    loading: false,
    q: "",
    category: "images",
    grid: null,
    epoch: 0,
    observer: null,
    statusEl: null,
  };

  function teardownImageFeed() {
    if (imageFeed.observer) {
      imageFeed.observer.disconnect();
      imageFeed.observer = null;
    }
    imageFeed.grid = null;
    imageFeed.statusEl = null;
    imageFeed.loading = false;
    imageFeed.hasMore = false;
  }

  function setImageFeedStatus(text) {
    if (!imageFeed.statusEl) {
      return;
    }
    if (!text) {
      imageFeed.statusEl.hidden = true;
      imageFeed.statusEl.textContent = "";
      return;
    }
    imageFeed.statusEl.hidden = false;
    imageFeed.statusEl.textContent = text;
  }

  function layoutImageGrid(grid) {
    if (!grid) {
      return;
    }
    const gap = 8;
    const rowGap = 12;
    const targetH = 160;
    // Prefer the visible content width; fall back to grid itself.
    const maxW = Math.floor(
      grid.clientWidth ||
        grid.parentElement?.clientWidth ||
        0,
    );
    if (maxW < 80) {
      return;
    }

    const items = [...grid.querySelectorAll(".serp-image.is-ready")];
    let row = [];
    let rowW = 0;

    const applyRow = (entries) => {
      if (!entries.length) {
        return;
      }
      const gaps = gap * Math.max(0, entries.length - 1);
      const usable = Math.max(1, maxW - gaps);
      const rawSum = entries.reduce((sum, item) => sum + item.w, 0) || 1;

      // Always stretch the row to the full container width (Brave/Google style).
      const widths = entries.map((item) =>
        Math.max(48, Math.floor((item.w / rawSum) * usable)),
      );
      let used = widths.reduce((sum, w) => sum + w, 0);
      let i = 0;
      while (used < usable && widths.length) {
        widths[i % widths.length] += 1;
        used += 1;
        i += 1;
      }
      // Guard against 1px overflow wrapping the last tile onto a new line.
      while (used > usable && widths.length) {
        const idx = widths.indexOf(Math.max(...widths));
        if (widths[idx] <= 48) {
          break;
        }
        widths[idx] -= 1;
        used -= 1;
      }

      entries.forEach((item, index) => {
        const width = widths[index];
        const img = item.el.querySelector(".serp-image-thumb");
        const isLast = index === entries.length - 1;
        item.el.style.flex = `0 0 ${width}px`;
        item.el.style.width = `${width}px`;
        item.el.style.maxWidth = `${width}px`;
        item.el.style.marginRight = isLast ? "0px" : `${gap}px`;
        item.el.style.marginBottom = `${rowGap}px`;
        if (img) {
          img.style.height = `${targetH}px`;
          img.style.width = "100%";
          img.style.maxWidth = "none";
        }
      });
    };

    const flush = () => {
      applyRow(row);
      row = [];
      rowW = 0;
    };

    items.forEach((el) => {
      const img = el.querySelector(".serp-image-thumb");
      if (!img) {
        return;
      }
      const nw = Math.max(1, img.naturalWidth || 160);
      const nh = Math.max(1, img.naturalHeight || targetH);
      const ratio = Math.min(3.2, Math.max(0.45, nw / nh));
      const w = ratio * targetH;
      if (row.length && rowW + gap + w > maxW) {
        flush();
      }
      row.push({ el, w });
      rowW += (row.length > 1 ? gap : 0) + w;
    });
    flush();
  }

  function bindImageGridLayout(grid) {
    let raf = 0;
    const relayout = () => {
      if (raf) {
        return;
      }
      raf = requestAnimationFrame(() => {
        raf = 0;
        layoutImageGrid(grid);
      });
    };
    grid.addEventListener("qubrain-img-settled", relayout);
    if (!grid._resizeBound) {
      grid._resizeBound = true;
      window.addEventListener("resize", () => {
        if (document.body.contains(grid)) {
          layoutImageGrid(grid);
        }
      });
    }
    relayout();
  }

  function appendImageResults(grid, items) {
    const sentinel = grid.querySelector(".serp-images-sentinel");
    items.forEach((item) => {
      if (!(item.thumbnail || item.image)) {
        return;
      }
      const node = renderImageResult(item);
      if (sentinel) {
        grid.insertBefore(node, sentinel);
      } else {
        grid.appendChild(node);
      }
    });
    layoutImageGrid(grid);
  }

  function renderImageResult(item) {
    const link = document.createElement("a");
    link.className = "serp-image";
    link.href = item.url;
    link.rel = "noopener noreferrer";
    link.target = "_blank";
    link.title = item.title || "";
    link.setAttribute("aria-label", item.title || "Image result");

    const img = document.createElement("img");
    img.className = "serp-image-thumb";
    img.alt = "";
    img.loading = "lazy";
    img.decoding = "async";
    bindResultImage(img, link, item.thumbnails || [item.thumbnail, item.image]);

    const meta = document.createElement("div");
    meta.className = "serp-image-meta";
    const favicon = document.createElement("img");
    favicon.className = "serp-image-favicon";
    favicon.alt = "";
    bindFavicon(favicon, item.url);
    const host = document.createElement("span");
    host.className = "serp-image-host";
    host.textContent = item.source || hostLabel(item.url);
    meta.append(favicon, host);

    const title = document.createElement("p");
    title.className = "serp-image-title";
    title.textContent = item.title;

    link.append(img, meta, title);
    return link;
  }

  async function loadMoreImages() {
    if (
      imageFeed.loading ||
      !imageFeed.hasMore ||
      !imageFeed.grid ||
      imageFeed.category !== "images"
    ) {
      return;
    }
    const snap = {
      q: imageFeed.q,
      grid: imageFeed.grid,
      epoch: imageFeed.epoch,
      nextPage: imageFeed.page + 1,
    };
    imageFeed.loading = true;
    setImageFeedStatus(LABELS.loadingMore);
    try {
      const data = await fetchSearchResults(snap.q, snap.nextPage, "images");
      if (
        imageFeed.epoch !== snap.epoch ||
        imageFeed.grid !== snap.grid ||
        imageFeed.q !== snap.q ||
        snap.epoch !== searchEpoch
      ) {
        return;
      }
      const mapped = mapApiResults(data?.results || []);
      if (mapped.length) {
        appendImageResults(snap.grid, mapped);
      }
      imageFeed.page = data?.page || snap.nextPage;
      imageFeed.hasMore = Boolean(data?.hasMore) && mapped.length > 0;
      setImageFeedStatus("");
    } catch (err) {
      console.error("image load-more failed", err);
      if (imageFeed.epoch === snap.epoch && imageFeed.grid === snap.grid) {
        setImageFeedStatus(
          wikiLang === "de"
            ? "Weitere Bilder konnten nicht geladen werden."
            : "Could not load more images.",
        );
        imageFeed.hasMore = false;
      }
    } finally {
      if (imageFeed.epoch === snap.epoch) {
        imageFeed.loading = false;
      }
    }
  }

  function setupImageInfiniteScroll(grid, q, page, hasMore) {
    teardownImageFeed();
    imageFeed = {
      page,
      hasMore,
      loading: false,
      q,
      category: "images",
      grid,
      epoch: searchEpoch,
      observer: null,
      statusEl: null,
    };

    let sentinel = grid.querySelector(".serp-images-sentinel");
    if (!sentinel) {
      sentinel = document.createElement("div");
      sentinel.className = "serp-images-sentinel";
      grid.appendChild(sentinel);
    }

    const status = document.createElement("p");
    status.className = "serp-images-status";
    status.hidden = true;
    grid.appendChild(status);
    imageFeed.statusEl = status;

    if (!hasMore) {
      return;
    }

    imageFeed.observer = new IntersectionObserver(
      (entries) => {
        if (entries.some((e) => e.isIntersecting)) {
          loadMoreImages();
        }
      },
      { root: null, rootMargin: "600px 0px", threshold: 0 },
    );
    imageFeed.observer.observe(sentinel);
  }

  function searchApiBase() {
    try {
      const override = localStorage.getItem("qubrain.searchApi");
      if (override && /^https?:\/\//i.test(override)) {
        return override.replace(/\/+$/, "");
      }
    } catch (_) {
      // ignore
    }
    return "https://api.qubrain.org";
  }

  window.qubrainSearchApiBase = searchApiBase;

  async function fetchSearchResults(
    q,
    page = 1,
    category = "general",
    signal = null,
  ) {
    const base = searchApiBase();
    const limit =
      category === "images" || category === "videos"
        ? 24
        : category === "map"
          ? mapResultLimit()
          : PAGE_SIZE();
    const url =
      `${base}/v1/search?q=${encodeURIComponent(q)}` +
      `&limit=${limit}&page=${Math.max(1, page)}` +
      `&category=${encodeURIComponent(category || "general")}`;
    const response = await fetch(url, {
      headers: { Accept: "application/json" },
      signal: signal || undefined,
    });
    if (!response.ok) {
      throw new Error(`Search API HTTP ${response.status}`);
    }
    const data = await response.json();
    writeResultCache(q, page, category, data);
    return data;
  }

  function goToPage(page, category = categoryParam) {
    const q = String(query || (input && input.value) || "").trim();
    if (!q) {
      return;
    }
    categoryParam = normalizeCategory(category);
    pageParam = Math.max(1, page || 1);
    query = q;
    syncTabUi(categoryParam);
    replaceSearchUrl(q, pageParam, categoryParam);
    document.title = `${q} · QuBrain Search`;
    loadKnowledge(q, pageParam, categoryParam);
  }

  function setPager(hasMore, page) {
    if (!pagerEl) {
      return;
    }
    const current = Math.max(1, page || 1);
    const showPrev = current > 1;
    const showNext = Boolean(hasMore);
    const show = showPrev || showNext;
    pagerEl.hidden = !show;

    if (prevBtn) {
      prevBtn.hidden = !showPrev;
      prevBtn.disabled = false;
      prevBtn.textContent = LABELS.prevPage;
      prevBtn.dataset.prevPage = String(current - 1);
    }
    if (nextBtn) {
      nextBtn.hidden = !showNext;
      nextBtn.disabled = false;
      nextBtn.textContent = LABELS.nextPage;
      nextBtn.dataset.nextPage = String(current + 1);
    }
    if (show && resultsEl && pagerEl.parentElement === resultsEl) {
      resultsEl.appendChild(pagerEl);
    }
  }

  function memCacheKey(q, page, category) {
    const limitPart =
      category === "map" ? `:l${mapResultLimit()}` : "";
    return `${category || "general"}:${page || 1}${limitPart}:${String(q || "")
      .trim()
      .toLowerCase()}`;
  }

  function readResultCache(q, page, category) {
    const key = memCacheKey(q, page, category);
    if (resultMemCache.has(key)) {
      return resultMemCache.get(key);
    }
    try {
      const raw = sessionStorage.getItem(`qubrain.serp.${key}`);
      if (!raw) {
        return null;
      }
      const data = JSON.parse(raw);
      if (data?.results?.length) {
        resultMemCache.set(key, data);
        return data;
      }
    } catch (_) {
      // ignore
    }
    return null;
  }

  function writeResultCache(q, page, category, data) {
    if (!data?.results?.length) {
      return;
    }
    const key = memCacheKey(q, page, category);
    resultMemCache.set(key, data);
    try {
      sessionStorage.setItem(`qubrain.serp.${key}`, JSON.stringify(data));
    } catch (_) {
      // ignore quota
    }
  }

  function runPreviewJob(job) {
    return new Promise((resolve) => {
      previewWaiters.push({ job, resolve });
      drainPreviewJobs();
    });
  }

  function drainPreviewJobs() {
    while (previewActive < PREVIEW_CONCURRENCY && previewWaiters.length) {
      const next = previewWaiters.shift();
      previewActive += 1;
      Promise.resolve()
        .then(next.job)
        .then(next.resolve, () => next.resolve(""))
        .finally(() => {
          previewActive -= 1;
          drainPreviewJobs();
        });
    }
  }

  function syncTabUi(category) {
    if (!tabsEl) {
      return;
    }
    tabsEl.querySelectorAll(".serp-tab").forEach((el) => {
      el.classList.toggle(
        "is-active",
        el.getAttribute("data-cat") === category,
      );
    });
  }

  function replaceSearchUrl(q, page, category) {
    const url = new URL(window.location.href);
    if (q) {
      url.searchParams.set("q", q);
    } else {
      url.searchParams.delete("q");
    }
    if (page <= 1) {
      url.searchParams.delete("page");
    } else {
      url.searchParams.set("page", String(page));
    }
    if (!category || category === "general") {
      url.searchParams.delete("tab");
      url.searchParams.delete("category");
    } else {
      url.searchParams.set("tab", category);
      url.searchParams.delete("category");
    }
    history.replaceState({}, "", url.toString());
  }

  function paintSkeletons(category = categoryParam) {
    if (!resultsEl) {
      return;
    }
    lastPaintKey = "";
    teardownImageFeed();
    clearResultNodes();
    if (emptyEl) {
      emptyEl.hidden = true;
    }
    const isImages = category === "images";
    const isNews = category === "news";
    const isMaps = category === "map";
    const isMedia = isImages || category === "videos";
    resultsEl.classList.toggle("is-media", isMedia);
    document.querySelector(".serp")?.classList.toggle("is-images", isImages);
    document.querySelector(".serp")?.classList.toggle("is-news", isNews);
    document.querySelector(".serp")?.classList.toggle("is-maps", isMaps);
    setPager(false, 1);

    const wrap = document.createElement("div");
    wrap.className = "serp-skeleton";
    wrap.setAttribute("aria-hidden", "true");

    if (isMaps) {
      wrap.style.cssText =
        "display:grid;grid-template-columns:minmax(280px,360px) 1fr;height:100%;width:100%;";
      const side = document.createElement("div");
      side.style.cssText = "padding:1rem;display:flex;flex-direction:column;gap:0.75rem;";
      for (let i = 0; i < 5; i += 1) {
        const block = document.createElement("div");
        block.className = "serp-skeleton-lines";
        ["is-title", "is-snip", "is-meta"].forEach((cls) => {
          const line = document.createElement("div");
          line.className = `serp-skeleton-line ${cls}`;
          block.appendChild(line);
        });
        side.appendChild(block);
      }
      const mapSkel = document.createElement("div");
      mapSkel.className = "serp-skeleton-image";
      mapSkel.style.cssText = "width:100%;height:100%;border-radius:0;animation:serp-shimmer 1.1s ease-in-out infinite;";
      wrap.append(side, mapSkel);
    } else if (isImages) {
      wrap.classList.add("serp-skeleton-images");
      const widths = [220, 160, 280, 190, 240, 150, 210, 170, 260, 200];
      widths.forEach((w) => {
        const cell = document.createElement("div");
        cell.className = "serp-skeleton-image";
        cell.style.width = `${w}px`;
        wrap.appendChild(cell);
      });
    } else if (isNews || category === "videos") {
      for (let i = 0; i < 6; i += 1) {
        const row = document.createElement("div");
        row.className = "serp-skeleton-row";
        row.style.marginBottom = "1.2rem";
        const lines = document.createElement("div");
        lines.className = "serp-skeleton-lines";
        ["is-meta", "is-title", "is-snip", "is-snip"].forEach((cls) => {
          const line = document.createElement("div");
          line.className = `serp-skeleton-line ${cls}`;
          lines.appendChild(line);
        });
        const thumb = document.createElement("div");
        thumb.className = "serp-skeleton-thumb";
        if (category === "videos") {
          thumb.style.width = "168px";
          thumb.style.height = "94px";
          thumb.style.borderRadius = "10px";
        }
        row.append(lines, thumb);
        wrap.appendChild(row);
      }
    } else {
      for (let i = 0; i < 6; i += 1) {
        const block = document.createElement("div");
        block.className = "serp-skeleton-lines";
        block.style.marginBottom = "1.45rem";
        ["is-meta", "is-title", "is-snip", "is-snip"].forEach((cls) => {
          const line = document.createElement("div");
          line.className = `serp-skeleton-line ${cls}`;
          if (cls === "is-title") {
            line.style.width = `${70 + (i % 3) * 8}%`;
          }
          block.appendChild(line);
        });
        wrap.appendChild(block);
      }
    }

    resultsEl.insertBefore(wrap, pagerEl || null);
  }

  function clearResultNodes() {
    if (!resultsEl) {
      return;
    }
    resultsEl
      .querySelectorAll(
        ".serp-result, .serp-images, .serp-videos, .serp-news-list, .serp-images-status, .serp-skeleton, .serp-maps, .serp-maps-host",
      )
      .forEach((node) => node.remove());
  }

  function formatRelativeTime(raw) {
    if (!raw) {
      return "";
    }
    const date = new Date(raw);
    if (Number.isNaN(date.getTime())) {
      return "";
    }
    const diffSec = Math.round((Date.now() - date.getTime()) / 1000);
    const abs = Math.abs(diffSec);
    const rtf = new Intl.RelativeTimeFormat(wikiLang === "de" ? "de" : "en", {
      numeric: "auto",
    });
    if (abs < 60) {
      return rtf.format(-diffSec, "second");
    }
    if (abs < 3600) {
      return rtf.format(-Math.round(diffSec / 60), "minute");
    }
    if (abs < 86400) {
      return rtf.format(-Math.round(diffSec / 3600), "hour");
    }
    if (abs < 86400 * 30) {
      return rtf.format(-Math.round(diffSec / 86400), "day");
    }
    if (abs < 86400 * 365) {
      return rtf.format(-Math.round(diffSec / (86400 * 30)), "month");
    }
    return rtf.format(-Math.round(diffSec / (86400 * 365)), "year");
  }

  function upgradeClientNewsThumb(src) {
    if (!src) {
      return "";
    }
    try {
      const u = new URL(src);
      if (/reuters\.com$/i.test(u.hostname.replace(/^www\./, ""))) {
        const h = Number(u.searchParams.get("height") || "0");
        if (h > 0 && h < 200) {
          u.searchParams.set("height", "240");
        }
      }
      return u.toString();
    } catch (_) {
      return src;
    }
  }

  function newsImageCandidates(item) {
    const raw = [
      ...(Array.isArray(item.thumbnails) ? item.thumbnails : []),
      item.thumbnail,
      item.image,
    ]
      .filter(Boolean)
      .map(upgradeClientNewsThumb);
    return [...new Set(raw)];
  }

  async function resolveNewsPreviewImage(pageUrl) {
    if (!pageUrl) {
      return "";
    }
    if (previewMemCache.has(pageUrl)) {
      return previewMemCache.get(pageUrl);
    }
    return runPreviewJob(async () => {
      if (previewMemCache.has(pageUrl)) {
        return previewMemCache.get(pageUrl);
      }
      try {
        const res = await fetch(
          `${searchApiBase()}/v1/preview-image?u=${encodeURIComponent(pageUrl)}`,
        );
        if (!res.ok) {
          previewMemCache.set(pageUrl, "");
          return "";
        }
        const data = await res.json();
        const image = data?.image ? String(data.image) : "";
        previewMemCache.set(pageUrl, image);
        return image;
      } catch (_) {
        previewMemCache.set(pageUrl, "");
        return "";
      }
    });
  }

  function bindNewsThumb(thumb, thumbWrap, item) {
    const candidates = newsImageCandidates(item);
    const ordered = [];
    for (const src of candidates) {
      ordered.push(imageProxyUrl(src), src);
    }

    let index = 0;
    let settled = false;
    const allowPreview = !thumbWrap.dataset.skipPreview;

    const finish = (ok) => {
      if (settled) {
        return;
      }
      settled = true;
      if (ok) {
        thumbWrap.classList.add("is-ready");
        thumbWrap.classList.remove("is-empty");
      } else {
        thumbWrap.classList.remove("is-ready");
        thumbWrap.classList.add("is-empty");
      }
    };

    const loadSrc = (src, onFail) => {
      thumb.referrerPolicy = "no-referrer";
      thumb.decoding = "async";
      thumb.onload = () => finish(true);
      thumb.onerror = onFail;
      thumb.src = src;
    };

    const tryPreview = () => {
      if (!allowPreview) {
        finish(false);
        return;
      }
      resolveNewsPreviewImage(item.url).then((preview) => {
        if (!preview) {
          finish(false);
          return;
        }
        const proxied = imageProxyUrl(preview);
        loadSrc(preview, () => {
          loadSrc(proxied, () => finish(false));
        });
      });
    };

    const tryNext = () => {
      if (index >= ordered.length) {
        tryPreview();
        return;
      }
      const next = ordered[index++];
      loadSrc(next, tryNext);
    };

    if (ordered.length) {
      tryNext();
    } else {
      tryPreview();
    }
  }

  function renderNewsResult(item, index = 0) {
    const link = document.createElement("a");
    link.className = "serp-news";
    link.href = item.url;
    link.rel = "noopener noreferrer";
    link.target = "_blank";

    const body = document.createElement("div");
    body.className = "serp-news-body";

    const meta = document.createElement("div");
    meta.className = "serp-news-meta";

    const favicon = document.createElement("img");
    favicon.className = "serp-news-favicon";
    favicon.alt = "";
    bindFavicon(favicon, item.url);

    const source = document.createElement("span");
    source.className = "serp-news-source";
    source.textContent = item.source
      ? item.source.charAt(0).toUpperCase() + item.source.slice(1)
      : siteDisplayName(item.url, item.name) || hostLabel(item.url);

    meta.append(favicon, source);

    const relative = formatRelativeTime(item.published);
    if (relative) {
      const dot = document.createElement("span");
      dot.className = "serp-news-dot";
      dot.textContent = "·";
      const time = document.createElement("span");
      time.className = "serp-news-time";
      time.textContent = relative;
      meta.append(dot, time);
    }

    const title = document.createElement("h2");
    title.className = "serp-news-title";
    title.textContent = item.title;

    const snippet = document.createElement("p");
    snippet.className = "serp-news-snippet";
    snippet.textContent = truncateSnippet(item.snippet, 180);

    body.append(meta, title, snippet);
    link.appendChild(body);

    const thumbWrap = document.createElement("div");
    thumbWrap.className = "serp-news-thumb-wrap";
    const hasApiThumb = newsImageCandidates(item).length > 0;
    // Cap expensive og:image discovery so first paint stays smooth.
    if (!hasApiThumb && index >= 8) {
      // No thumb slot for deep results without an API image.
      return link;
    }
    const thumb = document.createElement("img");
    thumb.className = "serp-news-thumb";
    thumb.alt = "";
    thumb.decoding = "async";
    thumb.setAttribute("draggable", "false");
    thumbWrap.appendChild(thumb);
    link.appendChild(thumbWrap);
    bindNewsThumb(thumb, thumbWrap, item);

    return link;
  }

  function renderResult(item) {
    const link = document.createElement("a");
    link.className = "serp-result";
    link.href = item.url;
    link.rel = "noopener noreferrer";

    const source = document.createElement("div");
    source.className = "serp-result-source";

    const faviconWrap = document.createElement("span");
    faviconWrap.className = "serp-result-favicon-wrap";
    const favicon = document.createElement("img");
    favicon.className = "serp-result-favicon";
    bindFavicon(favicon, item.url);
    faviconWrap.appendChild(favicon);

    const site = document.createElement("div");
    site.className = "serp-result-site";

    const name = document.createElement("span");
    name.className = "serp-result-name";
    name.textContent = siteDisplayName(item.url, item.name);

    const url = document.createElement("span");
    url.className = "serp-result-url";
    url.textContent = item.displayUrl || hostLabel(item.url);

    site.append(name, url);
    source.append(faviconWrap, site);

    const title = document.createElement("h2");
    title.className = "serp-result-title";
    title.textContent = item.title;

    const snippet = document.createElement("p");
    snippet.className = "serp-result-snippet";
    snippet.textContent = truncateSnippet(item.snippet);

    link.append(source, title, snippet);
    return link;
  }

  function renderVideoResult(item) {
    const link = document.createElement("a");
    link.className = "serp-video";
    link.href = item.url;
    link.rel = "noopener noreferrer";
    link.target = "_blank";

    const thumb = document.createElement("img");
    thumb.className = "serp-video-thumb";
    thumb.alt = "";
    thumb.loading = "lazy";
    bindResultImage(thumb, link, item.thumbnails || [item.thumbnail, item.image]);

    const body = document.createElement("div");
    const title = document.createElement("h2");
    title.className = "serp-video-title";
    title.textContent = item.title;
    const meta = document.createElement("p");
    meta.className = "serp-video-meta";
    meta.textContent = item.source || hostLabel(item.url);
    const snippet = document.createElement("p");
    snippet.className = "serp-video-snippet";
    snippet.textContent = truncateSnippet(item.snippet, 180);
    body.append(title, meta, snippet);

    link.append(thumb, body);
    return link;
  }

  function resultsPaintKey(q, apiResults, meta) {
    const category = meta?.category || categoryParam || "general";
    const page = meta?.page || 1;
    const urls = (apiResults || [])
      .slice(0, 8)
      .map((r) => r?.url || "")
      .join("|");
    return `${category}|${page}|${q}|${urls}|${apiResults?.length || 0}`;
  }

  function paintResults(q, knowledge, apiResults, meta) {
    if (!resultsEl) {
      return;
    }
    const category = meta?.category || categoryParam || "general";
    if (apiResults !== undefined && apiResults !== null) {
      const key = resultsPaintKey(q, apiResults, meta);
      if (key && key === lastPaintKey) {
        const page = meta?.page || 1;
        const pageSize =
          category === "images" || category === "videos" ? 24 : PAGE_SIZE();
        const hasMore =
          typeof meta?.hasMore === "boolean"
            ? meta.hasMore
            : (apiResults || []).length >= pageSize;
        if (category !== "images" && category !== "map") {
          setPager(hasMore, page);
        }
        return;
      }
      lastPaintKey = key;
    } else {
      lastPaintKey = "";
    }

    teardownImageFeed();
    if (window.QuBrainMaps) {
      window.QuBrainMaps.destroy();
    }
    clearResultNodes();
    const isMedia = category === "images" || category === "videos";
    const isImages = category === "images";
    const isNews = category === "news";
    const isMaps = category === "map";
    resultsEl.classList.toggle("is-media", isMedia);
    document.querySelector(".serp")?.classList.toggle("is-images", isImages);
    document.querySelector(".serp")?.classList.toggle("is-news", isNews);
    document.querySelector(".serp")?.classList.toggle("is-maps", isMaps);

    if (apiResults === undefined) {
      if (emptyEl) {
        emptyEl.hidden = true;
      }
      setPager(false, 1);
      return;
    }

    const fromApi = mapApiResults(apiResults);

    // Maps must always mount (island + canvas). Empty SERP otherwise leaves
    // is-maps on while .serp-empty is CSS-hidden → blank gray screen.
    if (category === "map") {
      if (emptyEl) {
        emptyEl.hidden = true;
      }
      const host = document.createElement("div");
      host.className = "serp-maps-host";
      resultsEl.insertBefore(host, pagerEl || null);
      ensureMapsStack().then(() => {
        if (window.QuBrainMaps && document.body.contains(host)) {
          window.QuBrainMaps.mount(host, fromApi, q);
        }
      });
      setPager(false, meta?.page || 1);
      return;
    }

    if (fromApi.length === 0) {
      if (emptyEl) {
        emptyEl.hidden = false;
        emptyEl.textContent =
          apiResults === null
            ? wikiLang === "de"
              ? "Suche fehlgeschlagen. Bitte erneut versuchen."
              : "Search failed. Please try again."
            : wikiLang === "de"
              ? "Keine Ergebnisse gefunden."
              : "No results found.";
      }
      setPager(false, meta?.page || 1);
      return;
    }

    if (emptyEl) {
      emptyEl.hidden = true;
    }

    if (category === "images") {
      const grid = document.createElement("div");
      grid.className = "serp-images";
      resultsEl.insertBefore(grid, pagerEl || null);
      appendImageResults(grid, fromApi);
      bindImageGridLayout(grid);
      const page = meta?.page || 1;
      const hasMore =
        typeof meta?.hasMore === "boolean"
          ? meta.hasMore
          : fromApi.length >= 24;
      setupImageInfiniteScroll(grid, q, page, hasMore);
      setPager(false, page);
      return;
    }

    const frag = document.createDocumentFragment();
    if (category === "videos") {
      const list = document.createElement("div");
      list.className = "serp-videos";
      fromApi.forEach((item) => {
        list.appendChild(renderVideoResult(item));
      });
      frag.appendChild(list);
    } else if (category === "news") {
      const list = document.createElement("div");
      list.className = "serp-news-list";
      const newsItems = [...fromApi].sort((a, b) => {
        const ta = Date.parse(String(a.published || "")) || 0;
        const tb = Date.parse(String(b.published || "")) || 0;
        if (tb !== ta) {
          return tb - ta;
        }
        if (!ta && tb) {
          return 1;
        }
        if (ta && !tb) {
          return -1;
        }
        return 0;
      });
      newsItems.forEach((item, index) => {
        list.appendChild(renderNewsResult(item, index));
      });
      frag.appendChild(list);
    } else {
      fromApi.forEach((item) => {
        frag.appendChild(renderResult(item));
      });
    }
    resultsEl.insertBefore(frag, pagerEl || null);

    const page = meta?.page || 1;
    const pageSize =
      category === "images" || category === "videos" ? 24 : PAGE_SIZE();
    const hasMore =
      typeof meta?.hasMore === "boolean"
        ? meta.hasMore
        : fromApi.length >= pageSize;
    setPager(hasMore, page);
  }

  async function wikiJson(url) {
    const response = await fetch(url, {
      headers: { Accept: "application/json" },
    });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    return response.json();
  }

  async function searchWikipediaTitle(q) {
    const api = new URL(`https://${wikiLang}.wikipedia.org/w/api.php`);
    api.searchParams.set("action", "query");
    api.searchParams.set("list", "search");
    api.searchParams.set("srsearch", q);
    api.searchParams.set("srlimit", "5");
    api.searchParams.set("srnamespace", "0");
    api.searchParams.set("format", "json");
    api.searchParams.set("origin", "*");
    const data = await wikiJson(api.toString());
    const hits = data?.query?.search || [];
    return hits.map((hit) => hit.title).filter(Boolean);
  }

  async function fetchSummary(title) {
    const url = `https://${wikiLang}.wikipedia.org/api/rest_v1/page/summary/${encodeURIComponent(
      title
    )}`;
    return wikiJson(url);
  }

  function claimValues(claims, propId) {
    const list = claims?.[propId];
    if (!Array.isArray(list) || !list.length) {
      return [];
    }
    return list
      .map((claim) => claim?.mainsnak?.datavalue)
      .filter(Boolean);
  }

  function officialNameFromClaims(claims) {
    const values = claimValues(claims, "P1448");
    for (const dv of values) {
      if (dv.type === "monolingualtext" && dv.value?.text) {
        const lang = dv.value.language;
        if (lang === wikiLang || lang === "en") {
          return dv.value.text;
        }
      }
    }
    for (const dv of values) {
      if (dv.type === "monolingualtext" && dv.value?.text) {
        return dv.value.text;
      }
    }
    return "";
  }

  function commonsFileUrl(filename, width = 128) {
    const name = String(filename || "").trim().replace(/^File:/i, "");
    if (!name) {
      return "";
    }
    return `https://commons.wikimedia.org/wiki/Special:FilePath/${encodeURIComponent(
      name
    )}?width=${width}`;
  }

  function logoFromClaims(claims) {
    // P154 = logo image (what knowledge panels use). P18 is often a photo.
    const logo = claimValues(claims, "P154").find(
      (dv) => dv.type === "string" && dv.value
    );
    if (logo) {
      return commonsFileUrl(logo.value, 200);
    }
    return "";
  }

  function brandLogoFallback(website) {
    if (!website) {
      return "";
    }
    try {
      const host = new URL(website).hostname.replace(/^www\./, "");
      return `https://www.google.com/s2/favicons?domain=${encodeURIComponent(
        host
      )}&sz=128`;
    } catch (_) {
      return "";
    }
  }

  // Informational / corporate subdomains — search engines prefer the product homepage.
  const INFO_HOST_LABELS = new Set([
    "about",
    "info",
    "support",
    "help",
    "news",
    "blog",
    "careers",
    "jobs",
    "investors",
    "investor",
    "ir",
    "press",
    "ads",
    "cloud",
    "workspace",
    "developers",
    "developer",
    "docs",
    "policies",
    "policy",
    "privacy",
    "store",
    "shop",
    "status",
    "research",
    "ai",
  ]);

  function normalizeWebsiteUrl(raw) {
    try {
      const url = new URL(String(raw || "").trim());
      if (url.protocol !== "http:" && url.protocol !== "https:") {
        return "";
      }
      url.hash = "";
      // Keep homepage-ish URLs clean.
      if (url.pathname === "/") {
        url.search = "";
      }
      return url.toString();
    } catch (_) {
      return "";
    }
  }

  function brandToken(query, label) {
    const fromQuery = String(query || "")
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, "");
    if (fromQuery) {
      return fromQuery;
    }
    return String(label || "")
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, "");
  }

  function homepageCandidatesFromUrl(raw, brand) {
    const out = [];
    const normalized = normalizeWebsiteUrl(raw);
    if (!normalized) {
      return out;
    }
    out.push(normalized);
    try {
      const url = new URL(normalized);
      const host = url.hostname.replace(/^www\./, "").toLowerCase();
      const labels = host.split(".");
      // about.google / info.google → google.com (navigational homepage)
      if (labels.length >= 2 && INFO_HOST_LABELS.has(labels[0])) {
        const core = labels[1];
        out.push(`https://www.${core}.com/`);
        out.push(`https://${core}.com/`);
      }
      // Deep official pages → site root
      if (url.pathname && url.pathname !== "/") {
        out.push(`${url.protocol}//${url.host}/`);
      }
      if (brand && host.includes(brand)) {
        out.push(`https://www.${brand}.com/`);
        out.push(`https://${brand}.com/`);
      }
    } catch (_) {
      /* ignore */
    }
    return out;
  }

  function scoreWebsiteCandidate(raw, brand) {
    const normalized = normalizeWebsiteUrl(raw);
    if (!normalized) {
      return -Infinity;
    }
    try {
      const url = new URL(normalized);
      const host = url.hostname.replace(/^www\./, "").toLowerCase();
      const labels = host.split(".");
      const head = labels[0] || "";
      let score = 0;

      // Exact navigational match: google.com for query "google"
      if (brand && (host === `${brand}.com` || host === `${brand}.org` || host === `${brand}.net` || host === `${brand}.io`)) {
        score += 120;
      } else if (brand && head === brand) {
        score += 70;
      } else if (brand && host.includes(brand)) {
        score += 25;
      }

      if (INFO_HOST_LABELS.has(head)) {
        score -= 80;
      }
      // Brand TLDs like about.google still lose to .com homepage.
      if (labels.length === 2 && INFO_HOST_LABELS.has(labels[0])) {
        score -= 40;
      }

      const pathDepth = url.pathname.split("/").filter(Boolean).length;
      score -= pathDepth * 12;
      if (url.search) {
        score -= 8;
      }
      if (host.endsWith(".com")) {
        score += 8;
      }
      if (url.hostname.startsWith("www.")) {
        score += 2;
      }
      return score;
    } catch (_) {
      return -Infinity;
    }
  }

  /**
   * Pick the URL users expect in SERPs (google.com), not corporate hubs
   * (about.google / info.google) that Wikidata often lists first under P856.
   */
  function pickBestWebsite(claims, query, entityLabel) {
    const brand = brandToken(query, entityLabel);
    const official = claimValues(claims, "P856")
      .map((dv) => (dv.type === "string" ? dv.value : ""))
      .filter(Boolean);

    const candidates = new Set();
    official.forEach((url) => {
      homepageCandidatesFromUrl(url, brand).forEach((item) =>
        candidates.add(item)
      );
    });
    if (brand && /^[a-z0-9]+$/i.test(brand)) {
      // Navigational fallback used by Brave/Google for brand queries.
      candidates.add(`https://www.${brand}.com/`);
      candidates.add(`https://${brand}.com/`);
    }

    let best = "";
    let bestScore = -Infinity;
    candidates.forEach((url) => {
      const score = scoreWebsiteCandidate(url, brand);
      // Prefer URLs that came from Wikidata slightly when scores tie.
      const fromOfficial = official.some((item) => {
        try {
          return (
            new URL(item).hostname.replace(/^www\./, "") ===
            new URL(url).hostname.replace(/^www\./, "")
          );
        } catch (_) {
          return false;
        }
      });
      const adjusted = score + (fromOfficial ? 3 : 0);
      if (adjusted > bestScore) {
        bestScore = adjusted;
        best = normalizeWebsiteUrl(url);
      }
    });
    return best || null;
  }

  function instanceIds(claims) {
    return claimValues(claims, "P31")
      .map((dv) => (dv.type === "wikibase-entityid" ? dv.value?.id : null))
      .filter(Boolean);
  }

  function scoreEntity(entity, query) {
    const claims = entity.claims || {};
    const types = instanceIds(claims);
    const isOrg = types.some((id) => ORG_TYPE_IDS.has(id));
    const label =
      entity.labels?.[wikiLang]?.value ||
      entity.labels?.en?.value ||
      "";
    const description =
      entity.descriptions?.[wikiLang]?.value ||
      entity.descriptions?.en?.value ||
      "";
    const sitelink = entity.sitelinks?.[SITE_KEY]?.title;
    const q = String(query || "").toLowerCase();
    const blob = `${label} ${description}`.toLowerCase();

    let score = 0;
    if (isOrg) {
      score += 100;
    }
    if (sitelink) {
      score += 25;
    }
    if (officialNameFromClaims(claims)) {
      score += 20;
    }
    if (/\b(llc|inc\.?|corp\.?|ltd\.?|gmbh|ag|sas|bv)\b/i.test(blob)) {
      score += 15;
    }
    if (
      /\b(company|unternehmen|corporation|organization|organisation|business)\b/i.test(
        description
      )
    ) {
      score += 12;
    }
    // Prefer exact-ish label matches for the brand query.
    if (label.toLowerCase() === q) {
      score += 18;
    } else if (label.toLowerCase().startsWith(q)) {
      score += 10;
    }
    // Soft-penalize pure products / websites / people when orgs exist.
    if (types.includes("Q5")) {
      score -= 40;
    }
    if (types.includes("Q35127") || types.includes("Q386724")) {
      // website / work
      score -= 15;
    }
    return { score, isOrg, sitelink, label, description };
  }

  async function searchWikidataEntities(q) {
    const api = new URL("https://www.wikidata.org/w/api.php");
    api.searchParams.set("action", "wbsearchentities");
    api.searchParams.set("search", q);
    api.searchParams.set("language", wikiLang);
    api.searchParams.set("uselang", wikiLang);
    api.searchParams.set("type", "item");
    api.searchParams.set("limit", "5");
    api.searchParams.set("format", "json");
    api.searchParams.set("origin", "*");
    const data = await wikiJson(api.toString());
    return (data.search || []).map((item) => item.id).filter(Boolean);
  }

  async function fetchEntities(ids) {
    const unique = [...new Set(ids.filter(Boolean))];
    if (!unique.length) {
      return {};
    }
    const api = new URL("https://www.wikidata.org/w/api.php");
    api.searchParams.set("action", "wbgetentities");
    api.searchParams.set("ids", unique.join("|"));
    api.searchParams.set("props", "claims|labels|descriptions|sitelinks");
    api.searchParams.set("languages", `${wikiLang}|en`);
    api.searchParams.set("sitefilter", SITE_KEY);
    api.searchParams.set("format", "json");
    api.searchParams.set("origin", "*");
    const data = await wikiJson(api.toString());
    return data.entities || {};
  }

  async function resolveCompanyEntity(q) {
    const ids = await searchWikidataEntities(q);
    if (!ids.length) {
      return null;
    }
    const entities = await fetchEntities(ids);
    const ranked = ids
      .map((id) => {
        const entity = entities[id];
        if (!entity || entity.missing !== undefined) {
          return null;
        }
        const meta = scoreEntity(entity, q);
        return { id, entity, ...meta };
      })
      .filter(Boolean)
      .sort((a, b) => b.score - a.score);

    if (!ranked.length) {
      return null;
    }

    // Prefer best org with a Wikipedia article; else best overall with article.
    const bestOrg = ranked.find((item) => item.isOrg && item.sitelink);
    const bestAny = ranked.find((item) => item.sitelink);
    return bestOrg || bestAny || ranked[0];
  }

  function formatTimeValue(value) {
    const raw = String(value?.time || "");
    const match = raw.match(/^([+-]?\d+)-(\d{2})-(\d{2})/);
    if (!match) {
      return raw || "";
    }
    const year = Number(match[1]);
    const month = Number(match[2]);
    const day = Number(match[3]);
    const precision = Number(value.precision || 11);
    try {
      if (precision <= 9) {
        return String(Math.abs(year));
      }
      const date = new Date(
        Date.UTC(Math.abs(year), Math.max(0, month - 1), Math.max(1, day))
      );
      return new Intl.DateTimeFormat(wikiLang, {
        year: "numeric",
        month: precision >= 10 ? "long" : undefined,
        day: precision >= 11 ? "numeric" : undefined,
      }).format(date);
    } catch (_) {
      return `${day}.${month}.${Math.abs(year)}`;
    }
  }

  async function resolveEntityLabels(ids) {
    const unique = [...new Set(ids.filter(Boolean))];
    if (!unique.length) {
      return new Map();
    }
    const api = new URL("https://www.wikidata.org/w/api.php");
    api.searchParams.set("action", "wbgetentities");
    api.searchParams.set("ids", unique.join("|"));
    api.searchParams.set("props", "labels");
    api.searchParams.set("languages", `${wikiLang}|en`);
    api.searchParams.set("format", "json");
    api.searchParams.set("origin", "*");
    const data = await wikiJson(api.toString());
    const map = new Map();
    Object.entries(data.entities || {}).forEach(([id, entity]) => {
      const label =
        entity?.labels?.[wikiLang]?.value ||
        entity?.labels?.en?.value ||
        id;
      map.set(id, label);
    });
    return map;
  }

  function factsFromClaims(claims, labels) {
    const facts = [];
    FACT_PROPS.forEach((prop) => {
      const values = claimValues(claims, prop.id)
        .map((dv) => {
          if (dv.type === "wikibase-entityid" && dv.value?.id) {
            return labels.get(dv.value.id) || dv.value.id;
          }
          if (dv.type === "time") {
            return formatTimeValue(dv.value);
          }
          if (dv.type === "string") {
            return dv.value;
          }
          if (dv.type === "monolingualtext") {
            return dv.value?.text || "";
          }
          return "";
        })
        .filter(Boolean);
      if (values.length) {
        // If we already have legal form, skip generic "Art" when it duplicates company.
        if (
          prop.id === "P31" &&
          facts.some((fact) => fact.label === LABELS.legalForm)
        ) {
          return;
        }
        facts.push({ label: prop.label, value: values.slice(0, 2).join(", ") });
      }
    });
    return facts;
  }

  function collectFactEntityIds(claims) {
    const entityIds = [];
    FACT_PROPS.forEach((prop) => {
      claimValues(claims, prop.id).forEach((dv) => {
        if (dv.type === "wikibase-entityid" && dv.value?.id) {
          entityIds.push(dv.value.id);
        }
      });
    });
    return entityIds;
  }

  async function fetchKnowledge(q) {
    // Run Wikidata + Wikipedia search in parallel.
    const [resolved, wikiTitles] = await Promise.all([
      resolveCompanyEntity(q).catch(() => null),
      searchWikipediaTitle(q).catch(() => []),
    ]);

    let wikiTitle = resolved?.sitelink || null;
    let qid = resolved?.id || null;
    let claims = resolved?.entity?.claims || {};
    let displayTitle = "";

    if (resolved) {
      displayTitle =
        officialNameFromClaims(claims) || resolved.label || "";
    }
    if (!wikiTitle) {
      wikiTitle = wikiTitles[0] || null;
    }
    if (!wikiTitle) {
      return null;
    }

    const factIds = collectFactEntityIds(claims);

    // Summary + fact labels in parallel (biggest win after dropping Microlink).
    const [summary, labels] = await Promise.all([
      fetchSummary(wikiTitle),
      resolveEntityLabels(factIds),
    ]);

    if (!summary) {
      return null;
    }
    if (summary.type === "disambiguation" && !summary.extract) {
      return null;
    }
    if (
      summary.type ===
      "https://mediawiki.org/wiki/HyperSwitch/errors/not_found"
    ) {
      return null;
    }

    // Rare fallback: Wikipedia page has a Wikidata id we didn't resolve yet.
    if (!qid && summary.wikibase_item) {
      qid = summary.wikibase_item;
      try {
        const entities = await fetchEntities([qid]);
        claims = entities[qid]?.claims || {};
        displayTitle =
          officialNameFromClaims(claims) ||
          displayTitle ||
          entities[qid]?.labels?.[wikiLang]?.value ||
          entities[qid]?.labels?.en?.value ||
          "";
        const extraLabels = await resolveEntityLabels(
          collectFactEntityIds(claims)
        );
        extraLabels.forEach((value, key) => labels.set(key, value));
      } catch (_) {
        /* optional */
      }
    }

    const wikiUrl =
      summary?.content_urls?.desktop?.page ||
      `https://${wikiLang}.wikipedia.org/wiki/${encodeURIComponent(wikiTitle)}`;

    const website = pickBestWebsite(claims, q, displayTitle || summary.title);
    const facts = factsFromClaims(claims, labels);

    const pageTitle = summary.titles?.display
      ? summary.titles.display.replace(/<[^>]+>/g, "")
      : summary.title || wikiTitle;

    const title = displayTitle || pageTitle;

    const thumbnail =
      logoFromClaims(claims) ||
      brandLogoFallback(website) ||
      summary.thumbnail?.source ||
      summary.originalimage?.source ||
      "";

    return {
      title,
      description: summary.description || "",
      extract: summary.extract || "",
      thumbnail,
      website,
      wikiUrl,
      facts,
    };
  }

  function showSideLoading() {
    if (!sideEl) {
      return;
    }
    sideEl.hidden = false;
    sideEl.replaceChildren();
    const loading = document.createElement("p");
    loading.className = "serp-side-loading";
    loading.textContent = LABELS.loading;
    sideEl.appendChild(loading);
  }

  function showSideEmpty(message) {
    if (!sideEl) {
      return;
    }
    sideEl.hidden = false;
    sideEl.replaceChildren();
    const empty = document.createElement("p");
    empty.className = "serp-side-loading";
    empty.textContent = message;
    sideEl.appendChild(empty);
  }

  function renderKnowledge(info) {
    if (!sideEl) {
      return;
    }
    if (!info) {
      showSideEmpty(LABELS.none);
      return;
    }

    sideEl.hidden = false;
    sideEl.replaceChildren();

    const card = document.createElement("div");
    card.className = "serp-side-card";

    const head = document.createElement("div");
    head.className = "serp-side-head";

    const headText = document.createElement("div");
    headText.className = "serp-side-head-text";

    const heading = document.createElement("h2");
    heading.className = "serp-side-title";
    heading.textContent = info.title;

    headText.appendChild(heading);

    if (info.description) {
      const desc = document.createElement("p");
      desc.className = "serp-side-desc";
      desc.textContent = info.description;
      headText.appendChild(desc);
    }

    if (info.website) {
      const site = document.createElement("a");
      site.className = "serp-side-site";
      site.href = info.website;
      site.rel = "noopener noreferrer";
      site.target = "_blank";
      site.innerHTML =
        '<svg viewBox="0 0 24 24" width="14" height="14" aria-hidden="true"><circle cx="12" cy="12" r="9" fill="none" stroke="currentColor" stroke-width="1.8"/><path d="M3 12h18M12 3c3 3.5 3 14.5 0 18M12 3c-3 3.5-3 14.5 0 18" fill="none" stroke="currentColor" stroke-width="1.8"/></svg>';
      const siteLabel = document.createElement("span");
      siteLabel.textContent = hostLabel(info.website);
      site.appendChild(siteLabel);
      headText.appendChild(site);
    }

    head.appendChild(headText);

    if (info.thumbnail) {
      const mark = document.createElement("img");
      mark.className = "serp-side-mark";
      mark.src = info.thumbnail;
      mark.alt = "";
      mark.loading = "lazy";
      mark.referrerPolicy = "no-referrer";
      mark.onerror = () => {
        mark.onerror = null;
        if (info.website) {
          mark.src = faviconUrlFor(info.website);
        } else {
          mark.remove();
        }
      };
      head.appendChild(mark);
    }

    card.appendChild(head);

    if (info.extract) {
      const text = document.createElement("p");
      text.className = "serp-side-text";
      text.textContent = info.extract + " ";
      if (info.wikiUrl) {
        const wikiLink = document.createElement("a");
        wikiLink.className = "serp-side-wiki-link";
        wikiLink.href = info.wikiUrl;
        wikiLink.rel = "noopener noreferrer";
        wikiLink.target = "_blank";
        wikiLink.textContent =
          info.linkLabel ||
          (/wikipedia\.org/i.test(info.wikiUrl) ? "Wikipedia" : LABELS.moreAbout);
        text.appendChild(wikiLink);
      }
      card.appendChild(text);
    }

    if (info.facts && info.facts.length) {
      const box = document.createElement("div");
      box.className = "serp-side-box";
      const facts = document.createElement("ul");
      facts.className = "serp-side-facts";
      info.facts.forEach((fact) => {
        const li = document.createElement("li");
        const k = document.createElement("span");
        k.textContent = fact.label;
        const v = document.createElement("strong");
        v.textContent = fact.value;
        li.append(k, v);
        facts.appendChild(li);
      });
      box.appendChild(facts);

      if (info.wikiUrl) {
        const more = document.createElement("a");
        more.className = "serp-side-more";
        more.href = info.wikiUrl;
        more.rel = "noopener noreferrer";
        more.target = "_blank";
        more.textContent = `${LABELS.moreAbout} ${info.title}`;
        box.appendChild(more);
      }

      card.appendChild(box);
    } else if (info.wikiUrl) {
      const more = document.createElement("a");
      more.className = "serp-side-more serp-side-more-solo";
      more.href = info.wikiUrl;
      more.rel = "noopener noreferrer";
      more.target = "_blank";
      more.textContent = `${LABELS.moreAbout} ${info.title}`;
      card.appendChild(more);
    }

    const note = document.createElement("p");
    note.className = "serp-side-note";
    note.textContent = info.sourceNote || LABELS.dataFrom;

    sideEl.append(card, note);
  }

  function knowledgeFromInfobox(ibox) {
    if (!ibox || (!ibox.title && !ibox.content)) {
      return null;
    }
    const urls = Array.isArray(ibox.urls) ? ibox.urls : [];
    const wikiUrl =
      ibox.url ||
      urls.find((u) => /wikipedia\.org/i.test(String(u.url || "")))?.url ||
      "";
    const website =
      urls.find(
        (u) =>
          u.url &&
          !/wikipedia\.org/i.test(u.url) &&
          !/wikidata\.org/i.test(u.url)
      )?.url || "";
    const facts = (Array.isArray(ibox.attributes) ? ibox.attributes : [])
      .filter((a) => a && a.label && a.value)
      .map((a) => ({ label: a.label, value: a.value }));

    const engine = String(ibox.engine || "").toLowerCase();
    let sourceNote = LABELS.dataFromSearx;
    if (engine === "wikipedia") {
      sourceNote = LABELS.dataFrom;
    } else if (engine) {
      sourceNote =
        wikiLang === "de"
          ? `Infobox von ${engine}`
          : `Infobox from ${engine}`;
    }

    return {
      title: ibox.title || "Info",
      description: "",
      extract: ibox.content || "",
      thumbnail: ibox.image || "",
      website,
      wikiUrl,
      facts,
      sourceNote,
      linkLabel: wikiUrl && /wikipedia\.org/i.test(wikiUrl) ? "Wikipedia" : "",
    };
  }

  async function loadKnowledge(q, page = 1, category = categoryParam) {
    const epoch = ++searchEpoch;
    if (searchAbort) {
      try {
        searchAbort.abort();
      } catch (_) {
        // ignore
      }
    }
    searchAbort = typeof AbortController !== "undefined" ? new AbortController() : null;
    const signal = searchAbort ? searchAbort.signal : null;
    const isGeneral = !category || category === "general";
    if (page <= 1 && isGeneral) {
      showSideLoading();
    } else if (sideEl) {
      sideEl.hidden = true;
      sideEl.replaceChildren();
    }

    const cached = readResultCache(q, page, category);
    if (cached?.results?.length) {
      paintResults(q, null, cached.results, {
        page: cached.page || page,
        hasMore: Boolean(cached.hasMore),
        category: cached.category || category,
      });
    } else {
      paintSkeletons(category);
    }

    // Start wiki knowledge in parallel with search (general page 1 only).
    const knowledgePromise =
      page <= 1 && isGeneral && !cached?.infobox
        ? fetchKnowledge(q).catch(() => null)
        : null;

    let data = null;
    try {
      data = await fetchSearchResults(q, page, category, signal);
      if (epoch !== searchEpoch) {
        return;
      }
      paintResults(q, null, data?.results || [], {
        page: data?.page || page,
        hasMore: Boolean(data?.hasMore),
        category: data?.category || category,
      });
    } catch (err) {
      if (err && err.name === "AbortError") {
        return;
      }
      console.error("search API failed", err);
      if (epoch !== searchEpoch) {
        return;
      }
      if (!cached?.results?.length) {
        paintResults(q, null, null, { category });
      }
    }

    if (page > 1 || !isGeneral || epoch !== searchEpoch) {
      return;
    }

    const fromSearx = knowledgeFromInfobox(data?.infobox || cached?.infobox);
    if (fromSearx) {
      renderKnowledge(fromSearx);
      return;
    }

    try {
      const info = knowledgePromise
        ? await knowledgePromise
        : await fetchKnowledge(q);
      if (epoch !== searchEpoch) {
        return;
      }
      if (info) {
        renderKnowledge(info);
      } else {
        showSideEmpty(LABELS.none);
      }
    } catch (err) {
      console.error("knowledge panel failed", err);
      if (epoch === searchEpoch) {
        showSideEmpty(LABELS.none);
      }
    }
  }

  function render(q, page = 1, category = categoryParam) {
    document.title = q ? `${q} · QuBrain Search` : "QuBrain Search";
    if (input) {
      input.value = q;
    }

    if (!q) {
      if (emptyEl) {
        emptyEl.hidden = false;
      }
      clearResultNodes();
      setPager(false, 1);
      document.querySelector(".serp")?.classList.remove(
        "is-images",
        "is-news",
        "is-maps",
      );
      if (resultsEl) {
        resultsEl.classList.remove("is-media");
      }
      if (sideEl) {
        sideEl.hidden = true;
        sideEl.replaceChildren();
      }
      return;
    }

    loadKnowledge(q, page, category);
  }

  window.QuBrainSearch = {
    go(q, category) {
      const next = String(q || "").trim();
      query = next;
      pageParam = 1;
      categoryParam = normalizeCategory(category || categoryParam || "general");
      if (input) {
        input.value = next;
      }
      if (!next) {
        replaceSearchUrl("", 1, categoryParam);
        render("", 1, categoryParam);
        return;
      }
      syncTabUi(categoryParam);
      replaceSearchUrl(next, 1, categoryParam);
      render(next, 1, categoryParam);
    },
  };

  if (form) {
    form.addEventListener("submit", (event) => {
      event.preventDefault();
      const next = String(input && input.value ? input.value : "").trim();
      window.QuBrainSearch.go(next, categoryParam);
    });
  }

  if (tabsEl) {
    tabsEl.addEventListener("click", (event) => {
      const btn = event.target.closest(".serp-tab");
      if (!btn) {
        return;
      }
      const cat = normalizeCategory(btn.getAttribute("data-cat") || "general");
      const q = String((input && input.value) || query || "").trim();
      if (!q) {
        categoryParam = cat;
        syncTabUi(cat);
        const url = new URL(window.location.href);
        if (cat === "general") {
          url.searchParams.delete("tab");
        } else {
          url.searchParams.set("tab", cat);
        }
        url.searchParams.delete("page");
        history.replaceState({}, "", url.toString());
        return;
      }
      goToPage(1, cat);
    });
  }

  if (nextBtn) {
    nextBtn.addEventListener("click", () => {
      const nextPage = Math.max(2, Number(nextBtn.dataset.nextPage || "2") || 2);
      goToPage(nextPage, categoryParam);
    });
  }

  if (prevBtn) {
    prevBtn.addEventListener("click", () => {
      const prevPage = Math.max(1, Number(prevBtn.dataset.prevPage || "1") || 1);
      goToPage(prevPage, categoryParam);
    });
  }

  window.addEventListener("resize", () => {
    if (window.QuBrainMaps) {
      window.QuBrainMaps.resize();
    }
  });

  if (!MAPS_ENABLED && (rawCat === "map" || rawCat === "maps")) {
    try {
      const url = new URL(window.location.href);
      url.searchParams.delete("tab");
      url.searchParams.delete("category");
      history.replaceState({}, "", url.toString());
    } catch (_) {
      // ignore
    }
  }

  render(query, pageParam, categoryParam);

  if (input && query) {
    input.focus();
    const len = input.value.length;
    input.setSelectionRange(len, len);
  } else if (input) {
    input.focus();
  }
})();
