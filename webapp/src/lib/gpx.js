// GPX-tolkning i webblasaren. Filerna kommer fran var egen enhet, men tolkas
// anda med riktig XML-tolk - DOMParser finns ju - i stallet for regexp.

export function parseGpx(xmlText) {
  const doc = new DOMParser().parseFromString(xmlText, "application/xml");
  if (doc.querySelector("parsererror")) return null;

  const pts = [];
  for (const el of doc.getElementsByTagName("trkpt")) {
    const lat = parseFloat(el.getAttribute("lat"));
    const lon = parseFloat(el.getAttribute("lon"));
    if (Number.isFinite(lat) && Number.isFinite(lon)) pts.push([lat, lon]);
  }
  return pts;
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
