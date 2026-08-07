(() => {
  const state = {
    map: null,
    layer: null,
    markers: [],
    places: [],
    activeIndex: -1,
    canvas: null,
    listEl: null,
    detailEl: null,
    root: null,
    query: "",
    applyViewportLimits: null,
    userMarker: null,
    userAccuracy: null,
    permChipHost: null,
  };

  function ensureLeaflet() {
    return typeof window.L !== "undefined"
      ? Promise.resolve(window.L)
      : Promise.reject(new Error("Leaflet not loaded"));
  }

  function destroyMap() {
    if (state.map) {
      state.map.remove();
    }
    state.map = null;
    state.layer = null;
    state.markers = [];
    state.places = [];
    state.activeIndex = -1;
    state.canvas = null;
    state.listEl = null;
    state.detailEl = null;
    state.root = null;
    state.query = "";
    state.applyViewportLimits = null;
    state.userMarker = null;
    state.userAccuracy = null;
    state.permChipHost = null;
    window.QuBrainPermission?.hideChip?.();
  }

  function parseOsmFromUrl(url) {
    try {
      const m = String(url || "").match(
        /openstreetmap\.org\/(node|way|relation)\/(\d+)/i,
      );
      if (!m) {
        return { osmType: "", osmId: null };
      }
      return { osmType: m[1].toLowerCase(), osmId: Number(m[2]) };
    } catch (_) {
      return { osmType: "", osmId: null };
    }
  }

  function placeKind(place) {
    const raw = String(place?.placeType || "").toLowerCase();
    const title = String(place?.title || "").toLowerCase();
    const blob = `${raw} ${title}`;
    if (
      /supermarket|convenience|grocery|penny|edeka|aldi|lidl|rewe|netto|kaufland/.test(
        blob,
      ) ||
      raw.includes("shop=supermarket") ||
      raw.includes("shop=convenience")
    ) {
      return "shop";
    }
    if (/cafe|coffee|café|bakery|shop=bakery|amenity=cafe/.test(blob)) {
      return "cafe";
    }
    if (/restaurant|fast_food|food_court|amenity=restaurant|amenity=fast_food/.test(blob)) {
      return "restaurant";
    }
    if (/bar|pub|biergarten|amenity=bar|amenity=pub/.test(blob)) {
      return "bar";
    }
    if (/fuel|gas|tankstelle|amenity=fuel/.test(blob)) {
      return "fuel";
    }
    if (/bank|atm|amenity=bank|amenity=atm/.test(blob)) {
      return "bank";
    }
    if (/hotel|motel|guest_house|tourism=hotel/.test(blob)) {
      return "hotel";
    }
    if (/pharmacy|apotheke|amenity=pharmacy/.test(blob)) {
      return "pharmacy";
    }
    if (/hospital|clinic|doctors|amenity=hospital|amenity=clinic|amenity=doctors/.test(blob)) {
      return "health";
    }
    if (/parking|amenity=parking/.test(blob)) {
      return "parking";
    }
    if (/school|university|kindergarten|amenity=school|amenity=university/.test(blob)) {
      return "school";
    }
    if (/park|garden|leisure=park/.test(blob)) {
      return "park";
    }
    if (/shop=/.test(raw) || /shop/.test(blob)) {
      return "shop";
    }
    return "place";
  }

  function kindIconSvg(kind) {
    const common =
      'viewBox="0 0 24 24" width="17" height="17" fill="none" stroke="#fff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"';
    const paths = {
      shop: `<svg ${common}><path d="M4 9h16l-1.2 11H5.2L4 9z"/><path d="M8 9V6a4 4 0 0 1 8 0v3"/><path d="M9 13v4M15 13v4"/></svg>`,
      cafe: `<svg ${common}><path d="M5 9h11v6a4 4 0 0 1-4 4H9a4 4 0 0 1-4-4V9z"/><path d="M16 10h2.5a2.5 2.5 0 0 1 0 5H16"/><path d="M8 3s1 1 1 2-1 2-1 2M12 3s1 1 1 2-1 2-1 2"/></svg>`,
      restaurant: `<svg ${common}><path d="M7 3v8M5 5v4M9 5v4M7 11v10"/><path d="M16 3v18M16 3c2.2 0 4 1.8 4 5v1h-4"/></svg>`,
      bar: `<svg ${common}><path d="M6 4h12l-2.5 8H8.5L6 4z"/><path d="M12 12v8M8 20h8"/></svg>`,
      fuel: `<svg ${common}><rect x="4" y="4" width="9" height="16" rx="1.5"/><path d="M13 8h3.5A2.5 2.5 0 0 1 19 10.5V16a2 2 0 0 0 2 2"/><path d="M7 9h3"/></svg>`,
      bank: `<svg ${common}><path d="M3 10h18L12 4 3 10z"/><path d="M5 10v8M9 10v8M15 10v8M19 10v8M4 18h16"/></svg>`,
      hotel: `<svg ${common}><path d="M3 20V9h11v11"/><path d="M14 12h5v8"/><path d="M3 20h18M7 12h3"/></svg>`,
      pharmacy: `<svg ${common}><rect x="4" y="4" width="16" height="16" rx="3"/><path d="M12 8v8M8 12h8"/></svg>`,
      health: `<svg ${common}><path d="M12 4v16M4 12h16"/></svg>`,
      parking: `<svg ${common}><rect x="4" y="4" width="16" height="16" rx="3"/><path d="M9 17V7h4.2a3.2 3.2 0 0 1 0 6.4H9"/></svg>`,
      school: `<svg ${common}><path d="M2 10l10-5 10 5-10 5-10-5z"/><path d="M6 12v5c2 2 10 2 12 0v-5"/></svg>`,
      park: `<svg ${common}><path d="M12 21V11"/><path d="M12 11c-3.5 0-6-2-6-5 3.5 0 6 2 6 5z"/><path d="M12 11c3.5 0 6-2 6-5-3.5 0-6 2-6 5z"/></svg>`,
      place: `<svg ${common}><circle cx="12" cy="10" r="3"/><path d="M12 21s7-6.2 7-11a7 7 0 1 0-14 0c0 4.8 7 11 7 11z"/></svg>`,
    };
    return paths[kind] || paths.place;
  }

  function pinIcon(place, active) {
    const kind = placeKind(place);
    const icon = kindIconSvg(kind);
    return window.L.divIcon({
      className: "serp-maps-pin-wrap",
      html: `<span class="serp-maps-pin${active ? " is-active" : ""} serp-maps-pin-${kind}"><span class="serp-maps-pin-glyph">${icon}</span></span>`,
      iconSize: [40, 48],
      iconAnchor: [20, 46],
      popupAnchor: [0, -38],
    });
  }

  function escapeHtml(value) {
    return String(value || "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function isJunkAddress(value) {
    return !value || /^OpenStreetMap\s+(node|way|relation)\//i.test(value);
  }

  function formatOpeningHours(raw) {
    const value = String(raw || "").trim();
    if (!value) {
      return "";
    }
    if (/^24\/7$/i.test(value) || /^24 hours$/i.test(value)) {
      return "Open 24 hours";
    }
    const schedule = parseOpeningHoursSchedule(value);
    const today = schedule.find((row) => row.isToday);
    if (today?.hours) {
      return today.hours;
    }
    const first = value.split(";")[0].trim();
    const timeMatch = first.match(
      /(\d{1,2}:\d{2})\s*-\s*(\d{1,2}:\d{2})/,
    );
    if (!timeMatch) {
      return first.length > 42 ? `${first.slice(0, 40)}…` : first;
    }
    return `${to12Hour(timeMatch[1])} - ${to12Hour(timeMatch[2])}`;
  }

  function to12Hour(t) {
    const [hStr, m] = String(t).split(":");
    let h = Number(hStr);
    if (!Number.isFinite(h)) {
      return String(t);
    }
    const suffix = h >= 12 ? "PM" : "AM";
    h = h % 12;
    if (h === 0) {
      h = 12;
    }
    return `${h}:${m || "00"} ${suffix}`;
  }

  const DAY_LABELS = [
    "Sunday",
    "Monday",
    "Tuesday",
    "Wednesday",
    "Thursday",
    "Friday",
    "Saturday",
  ];
  const OSM_DAY = { Su: 0, Mo: 1, Tu: 2, We: 3, Th: 4, Fr: 5, Sa: 6 };

  function expandOsmDays(token) {
    const t = String(token || "").trim();
    if (!t || /^PH$/i.test(t)) {
      return [];
    }
    if (/^Mo-Su$/i.test(t) || /^24\/7$/i.test(t)) {
      return [0, 1, 2, 3, 4, 5, 6];
    }
    const range = t.match(/^([A-Za-z]{2})\s*-\s*([A-Za-z]{2})$/);
    if (range) {
      const a = OSM_DAY[range[1]];
      const b = OSM_DAY[range[2]];
      if (a == null || b == null) {
        return [];
      }
      const out = [];
      let i = a;
      for (let n = 0; n < 7; n += 1) {
        out.push(i);
        if (i === b) {
          break;
        }
        i = (i + 1) % 7;
      }
      return out;
    }
    const single = OSM_DAY[t];
    return single == null ? [] : [single];
  }

  function formatOsmTimeSpan(span) {
    const raw = String(span || "").trim();
    if (!raw || /^off$/i.test(raw) || /^closed$/i.test(raw)) {
      return "Closed";
    }
    if (/^24\/7$/i.test(raw) || raw === "00:00-24:00" || raw === "00:00-00:00") {
      return "Open 24 hours";
    }
    const parts = raw.split(",").map((p) => p.trim()).filter(Boolean);
    return parts
      .map((part) => {
        const m = part.match(/(\d{1,2}:\d{2})\s*-\s*(\d{1,2}:\d{2})/);
        if (!m) {
          return part;
        }
        return `${to12Hour(m[1])} – ${to12Hour(m[2])}`;
      })
      .join(", ");
  }

  /** Parse OSM opening_hours into Mon→Sun rows (Brave-style). */
  function parseOpeningHoursSchedule(raw) {
    const value = String(raw || "").trim();
    const todayIdx = new Date().getDay();
    const byDay = {
      0: "Closed",
      1: "Closed",
      2: "Closed",
      3: "Closed",
      4: "Closed",
      5: "Closed",
      6: "Closed",
    };
    if (!value) {
      return DAY_LABELS.map((label, i) => ({
        dayIndex: i,
        dayLabel: label,
        hours: "—",
        isToday: i === todayIdx,
      }));
    }
    if (/^24\/7$/i.test(value)) {
      for (let i = 0; i < 7; i += 1) {
        byDay[i] = "Open 24 hours";
      }
    } else {
      const rules = value.split(";").map((r) => r.trim()).filter(Boolean);
      for (const rule of rules) {
        if (/^PH\b/i.test(rule)) {
          continue;
        }
        const m = rule.match(/^([A-Za-z0-9,\-\s]+)\s+(.+)$/);
        if (!m) {
          continue;
        }
        const dayPart = m[1].trim();
        const timePart = m[2].trim();
        const hours = formatOsmTimeSpan(timePart);
        const dayTokens = dayPart.split(",").map((d) => d.trim()).filter(Boolean);
        for (const token of dayTokens) {
          for (const d of expandOsmDays(token)) {
            byDay[d] = hours;
          }
        }
      }
    }
    // Show Monday→Sunday like Brave (then wrap Sunday at end).
    const order = [1, 2, 3, 4, 5, 6, 0];
    return order.map((i) => ({
      dayIndex: i,
      dayLabel: DAY_LABELS[i],
      hours: byDay[i] || "Closed",
      isToday: i === todayIdx,
    }));
  }

  function isOsmUrl(url) {
    try {
      const host = new URL(url).hostname.replace(/^www\./, "").toLowerCase();
      return (
        host === "openstreetmap.org" ||
        host.endsWith(".openstreetmap.org") ||
        host === "osm.org" ||
        host.endsWith(".osm.org")
      );
    } catch (_) {
      return false;
    }
  }

  function realWebsite(place) {
    const candidates = [place?.website, place?.url].filter(Boolean);
    for (const url of candidates) {
      if (!isOsmUrl(url)) {
        return url;
      }
    }
    return "";
  }

  function popupHtml(place) {
    const title = escapeHtml(place.title || "Place");
    const hours = formatOpeningHours(place.openingHours);
    const address = !isJunkAddress(place.address)
      ? escapeHtml(place.address)
      : "";

    let metaRow = "";
    if (hours) {
      metaRow = `
        <div class="serp-maps-bubble-meta">
          <span class="serp-maps-bubble-clock" aria-hidden="true">
            <svg viewBox="0 0 24 24" width="14" height="14">
              <circle cx="12" cy="12" r="8" fill="none" stroke="currentColor" stroke-width="2"/>
              <path d="M12 8v4l2.5 1.5" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>
            </svg>
          </span>
          <span class="serp-maps-bubble-label">Opening hours</span>
          <span class="serp-maps-bubble-dot">·</span>
          <span class="serp-maps-bubble-pill">${escapeHtml(hours)}</span>
        </div>`;
    } else if (address) {
      metaRow = `<div class="serp-maps-bubble-address">${address}</div>`;
    } else {
      metaRow = `<div class="serp-maps-bubble-address is-muted">Loading place info…</div>`;
    }

    return `
      <div class="serp-maps-bubble">
        <div class="serp-maps-bubble-title">${title}</div>
        ${metaRow}
      </div>`;
  }

  function setActive(index) {
    state.activeIndex = index;
    if (state.listEl) {
      state.listEl.querySelectorAll(".serp-maps-item").forEach((el, i) => {
        el.classList.toggle("is-active", i === index);
      });
    }
    state.markers.forEach((marker, i) => {
      const place = state.places[i];
      marker.setIcon(pinIcon(place, i === index));
      marker.setZIndexOffset(i === index ? 500 : 0);
    });
  }

  function flyTo(index, { openPopup = true } = {}) {
    const place = state.places[index];
    const marker = state.markers[index];
    if (!place || !state.map || !marker) {
      return;
    }
    setActive(index);
    state.map.flyTo([place.lat, place.lon], Math.max(state.map.getZoom(), 14), {
      duration: 0.65,
    });
    if (openPopup) {
      marker.openPopup();
    }
  }

  function apiBase() {
    return (
      (typeof window.qubrainSearchApiBase === "function"
        ? window.qubrainSearchApiBase()
        : null) || "https://api.qubrain.org"
    );
  }

  const enrichQueue = [];
  let enrichActive = 0;
  const ENRICH_CONCURRENCY = 2;

  function enqueueEnrich(task) {
    enrichQueue.push(task);
    pumpEnrich();
  }

  function pumpEnrich() {
    while (enrichActive < ENRICH_CONCURRENCY && enrichQueue.length) {
      const task = enrichQueue.shift();
      enrichActive += 1;
      Promise.resolve()
        .then(task)
        .catch(() => {})
        .finally(() => {
          enrichActive -= 1;
          pumpEnrich();
        });
    }
  }

  async function enrichPlace(place, index, refs) {
    if (!place?.lat || !place?.lon) {
      return;
    }
    // Re-fetch when hours are still missing (common for supermarket ways).
    if (place._enriched && place.openingHours && !isJunkAddress(place.address)) {
      applyPlaceToList(place, refs);
      if (state.activeIndex === index) {
        renderDetail(place, index);
      }
      return;
    }
    try {
      const fromUrl = parseOsmFromUrl(place.url);
      const osmType = place.osmType || fromUrl.osmType || "";
      const osmId =
        place.osmId != null && Number.isFinite(Number(place.osmId))
          ? Number(place.osmId)
          : fromUrl.osmId;
      const params = new URLSearchParams({
        lat: String(place.lat),
        lon: String(place.lon),
      });
      if (place.title) {
        params.set("name", place.title);
      }
      if (osmType) {
        params.set("osmType", osmType);
      }
      if (osmId != null) {
        params.set("osmId", String(osmId));
      }
      const res = await fetch(`${apiBase()}/v1/place/details?${params}`);
      if (!res.ok) {
        return;
      }
      const data = await res.json();
      if (data?.address) {
        place.address = data.address;
      }
      if (data?.openingHours) {
        place.openingHours = data.openingHours;
      } else if (data?.opening_hours) {
        place.openingHours = data.opening_hours;
      }
      if (data?.phone) {
        place.phone = data.phone;
      }
      if (data?.website) {
        place.website = data.website;
      }
      if (data?.wikipedia) {
        place.wikipedia = data.wikipedia;
      }
      if (data?.placeType) {
        place.placeType = data.placeType;
      }
      if (data?.name && (!place.title || place.title.length < 2)) {
        place.title = data.name;
      }
      place._enriched = true;
      applyPlaceToList(place, refs);
      const marker = state.markers[index];
      if (marker) {
        marker.setPopupContent(popupHtml(place));
        marker.setIcon(pinIcon(place, state.activeIndex === index));
      }
      if (state.activeIndex === index) {
        renderDetail(place, index);
      }
    } catch (_) {
      // ignore
    }
  }

  function applyPlaceToList(place, refs) {
    if (!refs) {
      return;
    }
    if (refs.addressEl && place.address && !isJunkAddress(place.address)) {
      refs.addressEl.textContent = place.address;
    }
    if (refs.hoursRow) {
      const hours = formatOpeningHours(place.openingHours);
      if (hours) {
        refs.hoursRow.hidden = false;
        if (refs.hoursPill) {
          refs.hoursPill.textContent = hours;
        }
      } else {
        refs.hoursRow.hidden = true;
        refs.hoursRow.removeAttribute("data-has-hours");
      }
    }
    if (refs.titleEl && place.title) {
      refs.titleEl.textContent = place.title;
    }
  }

  function pinSvg() {
    const ns = "http://www.w3.org/2000/svg";
    const svg = document.createElementNS(ns, "svg");
    svg.setAttribute("viewBox", "0 0 24 24");
    svg.setAttribute("width", "14");
    svg.setAttribute("height", "14");
    svg.setAttribute("aria-hidden", "true");
    svg.innerHTML =
      '<path d="M12 22s7-7.2 7-12a7 7 0 1 0-14 0c0 4.8 7 12 7 12z" fill="none" stroke="currentColor" stroke-width="2"/><circle cx="12" cy="10" r="2.5" fill="none" stroke="currentColor" stroke-width="2"/>';
    return svg;
  }

  function clockSvg() {
    const ns = "http://www.w3.org/2000/svg";
    const svg = document.createElementNS(ns, "svg");
    svg.setAttribute("viewBox", "0 0 24 24");
    svg.setAttribute("width", "14");
    svg.setAttribute("height", "14");
    svg.setAttribute("aria-hidden", "true");
    svg.innerHTML =
      '<circle cx="12" cy="12" r="8" fill="none" stroke="currentColor" stroke-width="2"/><path d="M12 8v4l2.5 1.5" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>';
    return svg;
  }

  function mapPreviewUrl(place, { large = false } = {}) {
    const direct = place.thumbnail || place.image || "";
    if (direct) {
      return direct;
    }
    if (!Number.isFinite(place.lat) || !Number.isFinite(place.lon)) {
      return "";
    }
    const z = large ? 16 : 15;
    const latRad = (place.lat * Math.PI) / 180;
    const n = 2 ** z;
    const x = Math.floor(((place.lon + 180) / 360) * n);
    const y = Math.floor(
      ((1 -
        Math.log(Math.tan(latRad) + 1 / Math.cos(latRad)) / Math.PI) /
        2) *
        n,
    );
    return `https://a.basemaps.cartocdn.com/rastertiles/voyager/${z}/${x}/${y}@2x.png`;
  }

  function directionsUrl(place) {
    return `https://www.google.com/maps/dir/?api=1&destination=${encodeURIComponent(
      `${place.lat},${place.lon}`,
    )}`;
  }

  function googleMapsUrl(place) {
    const label = place.title || "Place";
    return `https://www.google.com/maps/search/?api=1&query=${encodeURIComponent(
      `${label} ${place.lat},${place.lon}`,
    )}`;
  }

  function hostLabel(url) {
    try {
      return new URL(url).hostname.replace(/^www\./, "");
    } catch (_) {
      return String(url || "").replace(/^https?:\/\//, "").split("/")[0];
    }
  }

  function closeDetail() {
    if (!state.detailEl) {
      return;
    }
    state.detailEl.hidden = true;
    state.detailEl.innerHTML = "";
    state.root?.classList.remove("has-detail");
  }

  function selectPlace(index) {
    const place = state.places[index];
    if (!place) {
      return;
    }
    flyTo(index, { openPopup: false });
    renderDetail(place, index);
    enrichPlace(place, index, null);
    loadWebResults(place, index);
  }

  function renderDetail(place, index) {
    if (!state.detailEl) {
      return;
    }
    state.detailEl.hidden = false;
    state.root?.classList.add("has-detail");

    const hoursSummary = formatOpeningHours(place.openingHours);
    const hasHours = Boolean(String(place.openingHours || "").trim());
    const schedule = hasHours
      ? parseOpeningHoursSchedule(place.openingHours)
      : [];
    const address =
      place.address && !isJunkAddress(place.address)
        ? place.address
        : place.snippet && !isJunkAddress(place.snippet)
          ? place.snippet
          : "";
    const preview = mapPreviewUrl(place, { large: true });
    const website = realWebsite(place);
    const phone = String(place.phone || "").trim();
    const description =
      place.description ||
      (address
        ? `${place.title || "Place"} at ${address}.`
        : `${place.title || "Place"}.`);

    state.detailEl.innerHTML = "";

    const head = document.createElement("div");
    head.className = "serp-maps-detail-head";
    const title = document.createElement("h2");
    title.className = "serp-maps-detail-title";
    title.textContent = place.title || "Place";
    const closeBtn = document.createElement("button");
    closeBtn.type = "button";
    closeBtn.className = "serp-maps-detail-close";
    closeBtn.setAttribute("aria-label", "Close");
    closeBtn.textContent = "×";
    closeBtn.addEventListener("click", closeDetail);
    head.append(title, closeBtn);

    const hero = document.createElement("div");
    hero.className = "serp-maps-detail-hero";
    if (preview) {
      const img = document.createElement("img");
      img.alt = "";
      img.loading = "lazy";
      img.decoding = "async";
      img.src = preview;
      img.onerror = () => {
        hero.classList.add("is-empty");
        img.remove();
      };
      hero.appendChild(img);
    } else {
      hero.classList.add("is-empty");
    }

    const meta = document.createElement("div");
    meta.className = "serp-maps-detail-meta";

    const hoursDrop = document.createElement("div");
    hoursDrop.className = "serp-maps-hours-drop";
    hoursDrop.hidden = !hasHours;

    const hoursToggle = document.createElement("button");
    hoursToggle.type = "button";
    hoursToggle.className = "serp-maps-hours-drop-toggle";
    hoursToggle.setAttribute("aria-expanded", "false");
    hoursToggle.disabled = !hasHours;
    hoursToggle.appendChild(clockSvg());
    const hoursLabel = document.createElement("span");
    hoursLabel.className = "serp-maps-hours-drop-label";
    hoursLabel.textContent = hoursSummary
      ? `Opening hours · ${hoursSummary}`
      : "Opening hours";
    hoursToggle.appendChild(hoursLabel);
    const chevron = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    chevron.setAttribute("viewBox", "0 0 24 24");
    chevron.setAttribute("width", "16");
    chevron.setAttribute("height", "16");
    chevron.setAttribute("aria-hidden", "true");
    chevron.classList.add("serp-maps-hours-drop-chevron");
    chevron.innerHTML =
      '<path d="M6 9l6 6 6-6" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>';
    if (hasHours) {
      hoursToggle.appendChild(chevron);
    }

    const hoursBody = document.createElement("div");
    hoursBody.className = "serp-maps-hours-drop-body";
    hoursBody.hidden = true;

    if (hasHours) {
      const rows = document.createElement("div");
      rows.className = "serp-maps-hours-rows";
      schedule.forEach((row) => {
        const line = document.createElement("div");
        line.className = `serp-maps-hours-row${row.isToday ? " is-today" : ""}`;
        const day = document.createElement("span");
        day.className = "serp-maps-hours-day";
        day.textContent = row.dayLabel;
        const time = document.createElement("span");
        time.className = "serp-maps-hours-time";
        time.textContent = row.hours;
        line.append(day, time);
        rows.appendChild(line);
      });

      const addrLine = document.createElement("div");
      addrLine.className = "serp-maps-hours-address";
      addrLine.appendChild(pinSvg());
      const addrSpan = document.createElement("span");
      addrSpan.textContent = address || "Address unavailable";
      addrLine.appendChild(addrSpan);

      hoursBody.append(rows, addrLine);

      const setOpen = (open) => {
        hoursDrop.classList.toggle("is-open", open);
        hoursBody.hidden = !open;
        hoursToggle.setAttribute("aria-expanded", open ? "true" : "false");
        hoursLabel.textContent = open
          ? "Opening hours"
          : hoursSummary
            ? `Opening hours · ${hoursSummary}`
            : "Opening hours";
        addrRow.hidden = open;
      };

      hoursToggle.addEventListener("click", () => {
        setOpen(hoursBody.hidden);
      });
    }

    hoursDrop.append(hoursToggle, hoursBody);
    if (!hasHours) {
      hoursDrop.hidden = true;
    }

    const addrRow = document.createElement("div");
    addrRow.className = "serp-maps-detail-row";
    addrRow.appendChild(pinSvg());
    const addrText = document.createElement("span");
    addrText.textContent = address || "Looking up address…";
    addrRow.appendChild(addrText);

    if (hasHours) {
      meta.append(hoursDrop, addrRow);
    } else {
      meta.append(addrRow);
    }

    const actions = document.createElement("div");
    actions.className = "serp-maps-detail-actions";
    actions.dataset.actions = "1";

    const makeAction = (label, href, { disabled = false } = {}) => {
      const el = disabled
        ? document.createElement("span")
        : document.createElement("a");
      el.className = `serp-maps-detail-btn${disabled ? " is-disabled" : ""}`;
      el.textContent = label;
      if (!disabled) {
        el.href = href;
        el.target = "_blank";
        el.rel = "noopener noreferrer";
      }
      return el;
    };

    actions.append(
      makeAction("Website", website || "#", { disabled: !website }),
      makeAction("Call", phone ? `tel:${phone.replace(/[^\d+]/g, "")}` : "#", {
        disabled: !phone,
      }),
      makeAction("Directions", directionsUrl(place)),
    );

    const more = document.createElement("div");
    more.className = "serp-maps-detail-more";
    const gmaps = document.createElement("a");
    gmaps.className = "serp-maps-detail-gmaps";
    gmaps.href = googleMapsUrl(place);
    gmaps.target = "_blank";
    gmaps.rel = "noopener noreferrer";
    gmaps.title = "Open in Google Maps";
    gmaps.setAttribute("aria-label", "Open in Google Maps");
    gmaps.textContent = "G";
    more.appendChild(gmaps);
    actions.appendChild(more);

    const descBlock = document.createElement("div");
    descBlock.className = "serp-maps-detail-section";
    const descH = document.createElement("h3");
    descH.textContent = "Description";
    const descP = document.createElement("p");
    descP.textContent = description;
    const tellMore = document.createElement("a");
    tellMore.className = "serp-maps-detail-outline";
    tellMore.href = website || googleMapsUrl(place);
    tellMore.target = "_blank";
    tellMore.rel = "noopener noreferrer";
    tellMore.textContent = "Tell me more";
    if (!website) {
      tellMore.classList.add("is-fallback");
    }
    descBlock.append(descH, descP, tellMore);

    const webBlock = document.createElement("div");
    webBlock.className = "serp-maps-detail-section";
    webBlock.dataset.webResults = "1";
    const webH = document.createElement("h3");
    webH.textContent = "Web Results";
    const webList = document.createElement("div");
    webList.className = "serp-maps-detail-web";
    webList.innerHTML =
      '<p class="serp-maps-detail-web-loading">Looking up related pages…</p>';
    webBlock.append(webH, webList);

    state.detailEl.append(
      head,
      hero,
      meta,
      actions,
      descBlock,
      webBlock,
    );

    if (place._webResults) {
      paintWebResults(webList, place._webResults);
      maybeApplyWebsiteFromWeb(place);
    }
  }

  function refreshDetailActions(place) {
    const actions = state.detailEl?.querySelector('[data-actions="1"]');
    if (!actions) {
      return;
    }
    const website = realWebsite(place);
    const phone = String(place.phone || "").trim();
    const websiteBtn = actions.querySelector(".serp-maps-detail-btn");
    if (websiteBtn) {
      if (website) {
        const a =
          websiteBtn.tagName === "A"
            ? websiteBtn
            : document.createElement("a");
        a.className = "serp-maps-detail-btn";
        a.textContent = "Website";
        a.href = website;
        a.target = "_blank";
        a.rel = "noopener noreferrer";
        if (websiteBtn !== a) {
          websiteBtn.replaceWith(a);
        }
      } else {
        const span = document.createElement("span");
        span.className = "serp-maps-detail-btn is-disabled";
        span.textContent = "Website";
        websiteBtn.replaceWith(span);
      }
    }
    const callBtn = actions.querySelectorAll(".serp-maps-detail-btn")[1];
    if (callBtn) {
      if (phone) {
        const a =
          callBtn.tagName === "A" ? callBtn : document.createElement("a");
        a.className = "serp-maps-detail-btn";
        a.textContent = "Call";
        a.href = `tel:${phone.replace(/[^\d+]/g, "")}`;
        a.target = "_blank";
        a.rel = "noopener noreferrer";
        if (callBtn !== a) {
          callBtn.replaceWith(a);
        }
      } else if (callBtn.tagName !== "SPAN") {
        const span = document.createElement("span");
        span.className = "serp-maps-detail-btn is-disabled";
        span.textContent = "Call";
        callBtn.replaceWith(span);
      }
    }
    const tellMore = state.detailEl?.querySelector(".serp-maps-detail-outline");
    if (tellMore && website) {
      tellMore.href = website;
      tellMore.classList.remove("is-fallback");
    }
  }

  function maybeApplyWebsiteFromWeb(place) {
    if (realWebsite(place)) {
      refreshDetailActions(place);
      return;
    }
    const hit = (place._webResults || []).find(
      (r) => r?.url && !isOsmUrl(r.url),
    );
    if (hit?.url) {
      place.website = hit.url;
      refreshDetailActions(place);
    }
  }

  function paintWebResults(container, items) {
    container.innerHTML = "";
    if (!items?.length) {
      const empty = document.createElement("p");
      empty.className = "serp-maps-detail-web-loading";
      empty.textContent = "No related web results.";
      container.appendChild(empty);
      return;
    }
    items.forEach((item) => {
      const row = document.createElement("a");
      row.className = "serp-maps-detail-web-item";
      row.href = item.url;
      row.target = "_blank";
      row.rel = "noopener noreferrer";

      const fav = document.createElement("img");
      fav.className = "serp-maps-detail-web-fav";
      fav.alt = "";
      fav.width = 16;
      fav.height = 16;
      try {
        const host = new URL(item.url).hostname;
        fav.src = `https://www.google.com/s2/favicons?domain=${encodeURIComponent(host)}&sz=32`;
      } catch (_) {
        fav.hidden = true;
      }

      const body = document.createElement("div");
      body.className = "serp-maps-detail-web-body";
      const host = document.createElement("div");
      host.className = "serp-maps-detail-web-host";
      host.textContent = hostLabel(item.url);
      const title = document.createElement("div");
      title.className = "serp-maps-detail-web-title";
      title.textContent = item.title || hostLabel(item.url);
      body.append(host, title);
      row.append(fav, body);
      container.appendChild(row);
    });
  }

  async function loadWebResults(place, index) {
    if (place._webLoading || place._webResults) {
      const list = state.detailEl?.querySelector(
        '[data-web-results="1"] .serp-maps-detail-web',
      );
      if (list && place._webResults) {
        paintWebResults(list, place._webResults);
      }
      return;
    }
    place._webLoading = true;
    const q = [place.title, place.address?.split(",")[0], state.query]
      .filter(Boolean)
      .join(" ")
      .trim();
    try {
      const res = await fetch(
        `${apiBase()}/v1/search?q=${encodeURIComponent(q)}&limit=4&category=general`,
      );
      if (!res.ok) {
        throw new Error("web search failed");
      }
      const data = await res.json();
      const items = (data?.results || [])
        .filter((r) => r?.url && r?.title && !isOsmUrl(r.url))
        .slice(0, 3)
        .map((r) => ({ title: r.title, url: r.url }));
      if (realWebsite(place)) {
        items.unshift({
          title: place.title || hostLabel(realWebsite(place)),
          url: realWebsite(place),
        });
      }
      const seen = new Set();
      place._webResults = items
        .filter((it) => {
          if (seen.has(it.url)) {
            return false;
          }
          seen.add(it.url);
          return true;
        })
        .slice(0, 3);
    } catch (_) {
      const site = realWebsite(place);
      place._webResults = site
        ? [{ title: place.title || hostLabel(site), url: site }]
        : [];
    } finally {
      place._webLoading = false;
    }
    if (state.activeIndex !== index) {
      return;
    }
    const list = state.detailEl?.querySelector(
      '[data-web-results="1"] .serp-maps-detail-web',
    );
    if (list) {
      paintWebResults(list, place._webResults);
    }
    maybeApplyWebsiteFromWeb(place);
  }

  function renderListItem(place, index) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "serp-maps-item";
    btn.dataset.index = String(index);

    const body = document.createElement("div");
    body.className = "serp-maps-item-body";

    const title = document.createElement("div");
    title.className = "serp-maps-item-title";
    title.textContent = place.title || "Place";

    const addrRow = document.createElement("div");
    addrRow.className = "serp-maps-item-row";
    addrRow.appendChild(pinSvg());
    const address = document.createElement("span");
    address.className = "serp-maps-item-address";
    const initial = isJunkAddress(place.address)
      ? isJunkAddress(place.snippet)
        ? "Looking up address…"
        : place.snippet
      : place.address || place.snippet || "Looking up address…";
    address.textContent = initial;
    addrRow.appendChild(address);

    const hoursRow = document.createElement("div");
    hoursRow.className = "serp-maps-item-row";
    hoursRow.appendChild(clockSvg());
    const hoursLabel = document.createElement("span");
    hoursLabel.className = "serp-maps-item-hours-label";
    hoursLabel.textContent = "Opening hours";
    const hoursPill = document.createElement("span");
    hoursPill.className = "serp-maps-item-pill";
    const hoursText = formatOpeningHours(place.openingHours);
    hoursPill.textContent = hoursText;
    hoursRow.append(hoursLabel, hoursPill);
    // Only show when we actually have hours — never an empty/placeholder row.
    hoursRow.hidden = !hoursText;

    body.append(title, addrRow);
    if (hoursText) {
      body.appendChild(hoursRow);
    } else {
      // Keep node for enrichPlace to reveal later if hours arrive.
      body.appendChild(hoursRow);
      hoursRow.hidden = true;
    }

    const thumb = document.createElement("img");
    thumb.className = "serp-maps-item-thumb";
    thumb.alt = "";
    thumb.loading = "lazy";
    thumb.decoding = "async";
    const preview = mapPreviewUrl(place);
    if (preview) {
      thumb.src = preview;
      thumb.onerror = () => {
        thumb.hidden = true;
      };
    } else {
      thumb.hidden = true;
    }

    btn.append(body, thumb);
    btn.addEventListener("click", () => selectPlace(index));
    enqueueEnrich(() => enrichPlace(place, index, {
      addressEl: address,
      hoursRow,
      hoursPill,
      titleEl: title,
    }));
    return btn;
  }

  async function locateUser(map, btn) {
    if (!map || !navigator.geolocation) {
      return;
    }
    const origin =
      (typeof location !== "undefined" && location.host) || "qubrain.org";
    let decision = "allow";
    if (window.QuBrainPermission?.request) {
      decision = await window.QuBrainPermission.request({
        origin,
        permission: "geolocation",
        title: "Know your location",
        chipLabel: "Use your location?",
        precise: true,
        chipHost: state.permChipHost,
        anchorEl: state.permChipHost || btn,
      });
    }
    if (decision !== "allow") {
      return;
    }

    btn?.classList.add("is-loading");
    navigator.geolocation.getCurrentPosition(
      (pos) => {
        btn?.classList.remove("is-loading");
        const lat = pos.coords.latitude;
        const lon = pos.coords.longitude;
        const accuracy = pos.coords.accuracy || 80;
        if (state.userAccuracy) {
          state.userAccuracy.remove();
        }
        if (state.userMarker) {
          state.userMarker.remove();
        }
        state.userAccuracy = window.L.circle([lat, lon], {
          radius: Math.max(40, Math.min(accuracy, 400)),
          color: "#4285f4",
          weight: 1,
          fillColor: "#4285f4",
          fillOpacity: 0.15,
          interactive: false,
        }).addTo(map);
        state.userMarker = window.L.circleMarker([lat, lon], {
          radius: 8,
          color: "#fff",
          weight: 2,
          fillColor: "#4285f4",
          fillOpacity: 1,
        }).addTo(map);
        map.flyTo([lat, lon], Math.max(map.getZoom(), 15), { duration: 0.8 });
      },
      () => {
        btn?.classList.remove("is-loading");
      },
      { enableHighAccuracy: true, timeout: 12000, maximumAge: 10000 },
    );
  }

  function currentMapLimit() {
    if (typeof window.qubrainMapResultLimit === "function") {
      return window.qubrainMapResultLimit();
    }
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

  function renderSideFooter() {
    const foot = document.createElement("div");
    foot.className = "serp-maps-side-foot";

    const label = document.createElement("label");
    label.className = "serp-maps-side-foot-label";
    label.htmlFor = "serp-maps-result-limit";
    label.textContent = "Results";

    const select = document.createElement("select");
    select.id = "serp-maps-result-limit";
    select.className = "serp-maps-side-foot-select";
    select.setAttribute("aria-label", "Number of map results");
    const current = currentMapLimit();
    [20, 40, 60, 80, 100].forEach((n) => {
      const opt = document.createElement("option");
      opt.value = String(n);
      opt.textContent = String(n);
      if (n === current) {
        opt.selected = true;
      }
      select.appendChild(opt);
    });
    // Allow custom stored values between options.
    if (![20, 40, 60, 80, 100].includes(current)) {
      const opt = document.createElement("option");
      opt.value = String(current);
      opt.textContent = String(current);
      opt.selected = true;
      select.appendChild(opt);
    }

    select.addEventListener("change", () => {
      const next = Math.min(100, Math.max(10, Number(select.value) || 40));
      try {
        localStorage.setItem("omni.maps.resultLimit", String(next));
      } catch (_) {
        // ignore
      }
      const q = state.query || "";
      if (q && window.QuBrainSearch?.go) {
        window.QuBrainSearch.go(q, "map");
      }
    });

    const hint = document.createElement("span");
    hint.className = "serp-maps-side-foot-hint";
    hint.textContent = "on map & list";

    foot.append(label, select, hint);
    return foot;
  }

  function renderSideSearch(query) {
    const wrap = document.createElement("div");
    wrap.className = "serp-maps-side-chrome";

    const search = document.createElement("div");
    search.className = "serp-maps-side-search";

    const logo = document.createElement("button");
    logo.type = "button";
    logo.className = "serp-maps-side-logo";
    logo.title = "Back to All results";
    logo.setAttribute("aria-label", "QuBrain — back to All results");
    const logoImg = document.createElement("img");
    logoImg.src = "assets/qubrain.svg";
    logoImg.alt = "";
    logoImg.width = 28;
    logoImg.height = 28;
    logoImg.draggable = false;
    logo.appendChild(logoImg);
    logo.addEventListener("click", () => {
      window.QuBrainSearch?.go(state.query || query || "", "general");
    });

    const chipHost = document.createElement("div");
    chipHost.className = "serp-maps-perm-slot";
    state.permChipHost = chipHost;
    window.QuBrainPermission?.setChipHost?.(chipHost);

    const form = document.createElement("form");
    form.className = "serp-maps-side-form";
    form.setAttribute("role", "search");
    form.autocomplete = "off";

    const input = document.createElement("input");
    input.className = "serp-maps-side-input";
    input.type = "text";
    input.name = "q";
    input.spellcheck = false;
    input.autocomplete = "off";
    input.setAttribute("aria-label", "Search maps");
    input.placeholder = "Search maps";
    input.value = query || "";

    const clear = document.createElement("button");
    clear.type = "button";
    clear.className = "serp-maps-side-clear";
    clear.setAttribute("aria-label", "Clear search");
    clear.textContent = "×";
    clear.hidden = !input.value;
    clear.addEventListener("click", () => {
      input.value = "";
      clear.hidden = true;
      input.focus();
    });
    input.addEventListener("input", () => {
      clear.hidden = !String(input.value || "").trim();
    });

    form.append(input, clear);
    form.addEventListener("submit", (event) => {
      event.preventDefault();
      const next = String(input.value || "").trim();
      if (!next) {
        return;
      }
      window.QuBrainSearch?.go(next, "map");
    });

    search.append(logo, chipHost, form);
    wrap.appendChild(search);
    return wrap;
  }

  function mount(container, places, query) {
    destroyMap();
    if (!container) {
      return null;
    }

    state.query = String(query || "").trim();

    const geoPlaces = (places || []).filter(
      (p) =>
        Number.isFinite(p.lat) &&
        Number.isFinite(p.lon) &&
        Math.abs(p.lat) <= 90 &&
        Math.abs(p.lon) <= 180,
    );

    const root = document.createElement("div");
    root.className = "serp-maps";
    state.root = root;

    const side = document.createElement("aside");
    side.className = "serp-maps-side";

    const chrome = renderSideSearch(state.query);

    const list = document.createElement("div");
    list.className = "serp-maps-list";
    state.listEl = list;

    if (!geoPlaces.length) {
      const empty = document.createElement("p");
      empty.className = "serp-maps-empty";
      empty.textContent =
        "No places found for this search. Try a city, store name, or address.";
      list.appendChild(empty);
    } else {
      geoPlaces.forEach((place, index) => {
        list.appendChild(renderListItem(place, index));
      });
    }

    side.append(chrome, list, renderSideFooter());

    const detail = document.createElement("aside");
    detail.className = "serp-maps-detail";
    detail.hidden = true;
    detail.setAttribute("aria-label", "Place details");
    state.detailEl = detail;

    const canvas = document.createElement("div");
    canvas.className = "serp-maps-canvas";
    canvas.setAttribute("role", "application");
    canvas.setAttribute("aria-label", "Map");
    state.canvas = canvas;

    root.append(canvas, side, detail);
    container.appendChild(root);

    ensureLeaflet()
      .then((L) => {
        const worldBounds = L.latLngBounds(
          L.latLng(-85.05112878, -180),
          L.latLng(85.05112878, 180),
        );
        const center = geoPlaces[0]
          ? [geoPlaces[0].lat, geoPlaces[0].lon]
          : [51.16, 10.45];
        const map = L.map(canvas, {
          zoomControl: false,
          attributionControl: true,
          worldCopyJump: false,
          maxBounds: worldBounds,
          maxBoundsViscosity: 1.0,
        }).setView(center, geoPlaces[0] ? 12 : 6);

        function applyViewportLimits() {
          const size = map.getSize();
          if (!size.x || !size.y) {
            return;
          }
          // Don't zoom out past the point where the world fills the canvas
          // (no empty margins left/right or top/bottom).
          const minForW = Math.log2(size.x / 256);
          const minForH = Math.log2(size.y / 256);
          const minZoom = Math.max(
            1,
            Math.ceil(Math.max(minForW, minForH) * 100) / 100,
          );
          map.setMinZoom(minZoom);
          if (map.getZoom() < minZoom) {
            map.setZoom(minZoom);
          }
          map.setMaxBounds(worldBounds);
        }

        L.control.zoom({ position: "topright" }).addTo(map);

        const LocateControl = L.Control.extend({
          options: { position: "topright" },
          onAdd() {
            const wrap = L.DomUtil.create(
              "div",
              "leaflet-bar serp-maps-locate-bar",
            );
            const btn = L.DomUtil.create(
              "button",
              "serp-maps-locate-btn",
              wrap,
            );
            btn.type = "button";
            btn.title = "Find my location";
            btn.setAttribute("aria-label", "Find my location");
            btn.innerHTML = `
              <svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true">
                <circle cx="12" cy="12" r="3" fill="none" stroke="currentColor" stroke-width="2"/>
                <path d="M12 3v3M12 18v3M3 12h3M18 12h3" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>
                <circle cx="12" cy="12" r="8" fill="none" stroke="currentColor" stroke-width="2"/>
              </svg>`;
            L.DomEvent.disableClickPropagation(wrap);
            L.DomEvent.on(btn, "click", (event) => {
              L.DomEvent.stop(event);
              locateUser(map, btn);
            });
            return wrap;
          },
        });
        new LocateControl().addTo(map);

        L.tileLayer(
          "https://{s}.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}{r}.png",
          {
            attribution:
              '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> &copy; <a href="https://carto.com/attributions">CARTO</a>',
            subdomains: "abcd",
            maxZoom: 20,
            noWrap: true,
            bounds: worldBounds,
          },
        ).addTo(map);

        const markers = [];
        const bounds = [];
        geoPlaces.forEach((place, index) => {
          if (isJunkAddress(place.address)) {
            place.address = "";
          }
          if (isJunkAddress(place.snippet)) {
            place.snippet = "";
          }
          const marker = L.marker([place.lat, place.lon], {
            icon: pinIcon(place, false),
            title: place.title,
          }).addTo(map);
          marker.bindPopup(popupHtml(place), {
            className: "serp-maps-popup",
            closeButton: false,
            offset: [0, -6],
            maxWidth: 320,
            minWidth: 180,
          });
          marker.on("click", () => {
            selectPlace(index);
            const item = list.querySelector(`[data-index="${index}"]`);
            item?.scrollIntoView({ block: "nearest", behavior: "smooth" });
          });
          markers.push(marker);
          bounds.push([place.lat, place.lon]);
        });

        state.map = map;
        state.markers = markers;
        state.places = geoPlaces;
        state.applyViewportLimits = applyViewportLimits;

        if (bounds.length > 1) {
          map.fitBounds(bounds, { padding: [48, 48], maxZoom: 14 });
        } else if (bounds.length === 1) {
          map.setView(bounds[0], 14);
        }

        const settle = () => {
          map.invalidateSize();
          applyViewportLimits();
        };
        requestAnimationFrame(settle);
        setTimeout(settle, 120);
        map.on("resize", applyViewportLimits);
      })
      .catch((err) => {
        console.error("maps init failed", err);
        canvas.innerHTML =
          '<p class="serp-maps-empty">Map failed to load.</p>';
      });

    return root;
  }

  function resize() {
    if (state.map) {
      state.map.invalidateSize();
      if (typeof state.applyViewportLimits === "function") {
        state.applyViewportLimits();
      }
    }
  }

  window.QuBrainMaps = {
    mount,
    destroy: destroyMap,
    resize,
  };
})();
