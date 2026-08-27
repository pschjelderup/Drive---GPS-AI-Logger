// GPX-tolkning i webblasaren. Filerna kommer fran var egen enhet, men tolkas
// anda med riktig XML-tolk - DOMParser finns ju - i stallet for regexp.

export function parseGpx(xmlText) {
  const doc = new DOMParser().parseFromString(xmlText, "application/xml");
  if (doc.querySelector("parsererror")) return null;

  // Segment for segment, med pennlyft (null) emellan: den rekonstruerade
  // kallstartsstrackan ligger i ett eget trkseg, och sommen mellan den och
  // det riktiga sparet ska inte ritas som om den vore kord i ett svep.
  const segs = Array.from(doc.getElementsByTagName("trkseg"));
  const sources = segs.length ? segs : [doc];
  const pts = [];
  for (const seg of sources) {
    let added = false;
    for (const el of seg.getElementsByTagName("trkpt")) {
      const lat = parseFloat(el.getAttribute("lat"));
      const lon = parseFloat(el.getAttribute("lon"));
      if (Number.isFinite(lat) && Number.isFinite(lon)) {
        pts.push([lat, lon]);
        added = true;
      }
    }
    if (added) pts.push(null);
  }
  if (pts.length && pts[pts.length - 1] === null) pts.pop();
  return pts;
}

// Som parseGpx, men med tidsstamplarna: [{lat, lon, t}] dar t ar millisekunder
// eller null. Tiderna behovs for fartlagret pa kartan - farten mellan tva
// punkter ar strackan delad med tiden.
export function parseGpxTimed(xmlText) {
  const doc = new DOMParser().parseFromString(xmlText, "application/xml");
  if (doc.querySelector("parsererror")) return null;

  const pts = [];
  for (const el of doc.getElementsByTagName("trkpt")) {
    const lat = parseFloat(el.getAttribute("lat"));
    const lon = parseFloat(el.getAttribute("lon"));
    if (!Number.isFinite(lat) || !Number.isFinite(lon)) continue;
    const timeEl = el.getElementsByTagName("time")[0];
    const t = timeEl ? Date.parse(timeEl.textContent) : NaN;
    pts.push({ lat, lon, t: Number.isFinite(t) ? t : null });
  }
  return pts;
}

// Fart mellan tva punkter, km/h, eller null nar tiderna inte later sig
// anvandas - identiska stamplar fran en frusen klocka ska ge "vet ej",
// aldrig en pahittad fart.
export function segmentSpeedKmh(a, b) {
  if (a.t == null || b.t == null) return null;
  const dtS = (b.t - a.t) / 1000;
  if (dtS < 1 || dtS > 300) return null;

  const R = 6371000;
  const rad = Math.PI / 180;
  const dLat = (b.lat - a.lat) * rad;
  const dLon = (b.lon - a.lon) * rad;
  const s = Math.sin(dLat / 2) ** 2 +
    Math.cos(a.lat * rad) * Math.cos(b.lat * rad) * Math.sin(dLon / 2) ** 2;
  const m = 2 * R * Math.asin(Math.sqrt(s));
  const kmh = (m / dtS) * 3.6;
  return kmh < 250 ? kmh : null;
}

// En rad ur RESOR.JSONL -> kolumnerna i drive_trips. Hela raden foljer med som
// raw, sa att uppackningen gar att gora om om den visar sig ha fel.
export function tripFromJsonl(line, deviceId) {
  let r;
  try {
    r = JSON.parse(line);
  } catch {
    return null;
  }
  if (!r || typeof r.resa !== "number") return null;

  const purpose = ["privat", "foretag", "diffust"].includes(r.syfte)
    ? r.syfte
    : "omarkt";

  return {
    device_id: deviceId,
    trip_no: r.resa,
    start_utc: r.start || null,
    end_utc: r.mal || null,
    start_lat: r.start_lat ?? null,
    start_lon: r.start_lon ?? null,
    end_lat: r.mal_lat ?? null,
    end_lon: r.mal_lon ?? null,
    distance_m: r.meter ?? 0,
    points: r.punkter ?? 0,
    purpose,
    customer: r.kund || null,
    max_speed_kmh: r.maxfart_kmh ?? null,
    speeding_s: r.fortkorning_s ?? 0,
    moving_s: r.rullande_s ?? 0,
    eco_score: r.ecopoang ?? null,
    hard_events: r.harda_moment ?? null,
    end_reason: r.avslut || null,
    gpx_name: r.gpx || null,
    raw: r,
  };
}

// Avstand i meter mellan tva koordinater. Anvands bland annat for att kanna
// igen en kunds kontor i en resas start- eller malpunkt.
export function distanceM(lat1, lon1, lat2, lon2) {
  const R = 6371000;
  const rad = Math.PI / 180;
  const dLat = (lat2 - lat1) * rad;
  const dLon = (lon2 - lon1) * rad;
  const s = Math.sin(dLat / 2) ** 2 +
    Math.cos(lat1 * rad) * Math.cos(lat2 * rad) * Math.sin(dLon / 2) ** 2;
  return 2 * R * Math.asin(Math.sqrt(s));
}
