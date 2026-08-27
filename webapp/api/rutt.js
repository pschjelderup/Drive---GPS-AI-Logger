// Ruttforslag: tva koordinater in, en korbar vag ut som punktlista.
// Journalen anvander den for att fylla kallstartsluckan - strackan fran
// parkeringen till dar gps-fixen kom. Google Routes ar forstahandsvalet
// (nyckeln ligger i Vercels miljovariabler och lamnar aldrig servern);
// ar Routes-API:t inte paslaget pa nyckeln faller vi tillbaka pa OSRM:s
// oppna demoserver. Bara inloggade slapps fram, som i de andra proxyerna.
export const config = { runtime: "edge" };

const SUPABASE_URL = "https://jdjkeloiwjkcycelmexq.supabase.co";
const SUPABASE_KEY = "sb_publishable_d3O3Vk2vwNNV8piYGEcffA_kuyZdKCW";

async function validSession(req) {
  const auth = req.headers.get("authorization") ?? "";
  if (!auth.startsWith("Bearer ")) return false;
  const r = await fetch(`${SUPABASE_URL}/auth/v1/user`, {
    headers: { apikey: SUPABASE_KEY, Authorization: auth },
  });
  return r.ok;
}

// Googles kodade polylinje -> [[lat, lon], ...]. Standardalgoritmen:
// varvade delta-varden i femtondelsgrader, teckenbit sist.
function decodePolyline(str) {
  const pts = [];
  let lat = 0, lon = 0, i = 0;
  while (i < str.length) {
    for (const which of [0, 1]) {
      let shift = 0, result = 0, b;
      do {
        b = str.charCodeAt(i++) - 63;
        result |= (b & 0x1f) << shift;
        shift += 5;
      } while (b >= 0x20);
      const d = (result & 1) ? ~(result >> 1) : (result >> 1);
      if (which === 0) lat += d; else lon += d;
    }
    pts.push([lat / 1e5, lon / 1e5]);
  }
  return pts;
}

async function viaGoogle(flat, flon, tlat, tlon, key) {
  const r = await fetch(
    "https://routes.googleapis.com/directions/v2:computeRoutes",
    {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "X-Goog-Api-Key": key,
        "X-Goog-FieldMask":
          "routes.distanceMeters,routes.duration,routes.polyline.encodedPolyline",
      },
      body: JSON.stringify({
        origin: { location: { latLng: { latitude: flat, longitude: flon } } },
        destination: { location: { latLng: { latitude: tlat, longitude: tlon } } },
        travelMode: "DRIVE",
      }),
    },
  );
  if (!r.ok) return null;
  const route = (await r.json()).routes?.[0];
  if (!route?.polyline?.encodedPolyline) return null;
  return {
    points: decodePolyline(route.polyline.encodedPolyline),
    meters: route.distanceMeters ?? 0,
    seconds: parseFloat(route.duration ?? "0") || 0,
  };
}

async function viaOsrm(flat, flon, tlat, tlon) {
  const u =
    `https://router.project-osrm.org/route/v1/driving/` +
    `${flon},${flat};${tlon},${tlat}?overview=full&geometries=geojson`;
  const r = await fetch(u, { headers: { "User-Agent": "Hikaya/1.0" } });
  if (!r.ok) return null;
  const route = (await r.json()).routes?.[0];
  if (!route?.geometry?.coordinates?.length) return null;
  return {
    points: route.geometry.coordinates.map(([lon, lat]) => [lat, lon]),
    meters: route.distance ?? 0,
    seconds: route.duration ?? 0,
  };
}

export default async function handler(req) {
  if (!(await validSession(req))) {
    return new Response(JSON.stringify({ error: "inte inloggad" }), { status: 401 });
  }

  const url = new URL(req.url);
  const flat = parseFloat(url.searchParams.get("flat"));
  const flon = parseFloat(url.searchParams.get("flon"));
  const tlat = parseFloat(url.searchParams.get("tlat"));
  const tlon = parseFloat(url.searchParams.get("tlon"));
  if (![flat, flon, tlat, tlon].every(Number.isFinite)) {
    return new Response(JSON.stringify({ error: "flat/flon/tlat/tlon krävs" }), {
      status: 400,
    });
  }

  const key = process.env.GOOGLE_MAPS_API_KEY;
  let route = null;
  if (key) {
    try { route = await viaGoogle(flat, flon, tlat, tlon, key); } catch { /* reserven tar det */ }
  }
  if (!route) {
    try { route = await viaOsrm(flat, flon, tlat, tlon); } catch { /* svaret nedan */ }
  }
  if (!route || route.points.length < 2) {
    return new Response(JSON.stringify({ error: "ingen rutt hittades" }), {
      status: 404,
    });
  }

  return new Response(JSON.stringify(route), {
    headers: { "Content-Type": "application/json" },
  });
}
