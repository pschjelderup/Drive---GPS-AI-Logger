// Omvand geokodning: koordinat in, gatuadress ut. Journalen anvander den for
// att satta adress pa varje resas start och mal nar nya resor synkats in.
// Google ar forstahandsvalet - nyckeln ligger i Vercels miljovariabler och
// lamnar aldrig servern - men ar Geocoding-API:t inte paslaget pa nyckeln
// faller vi tillbaka pa OpenStreetMaps Nominatim, sa att adresserna kommer
// aven utan nyckel. Bara inloggade slapps fram, som i de andra proxyerna.
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

// "Storgatan 5, 972 38 Luleå, Sverige" -> "Storgatan 5, 972 38 Luleå".
// Landet ar underforstatt i en svensk korjournal.
function trimCountry(addr) {
  return addr.replace(/,\s*(Sverige|Sweden)$/i, "");
}

async function viaGoogle(lat, lon, key) {
  const u =
    `https://maps.googleapis.com/maps/api/geocode/json?latlng=${lat},${lon}` +
    `&language=sv&result_type=street_address|route|premise|point_of_interest` +
    `&key=${key}`;
  const r = await fetch(u);
  if (!r.ok) return null;
  const data = await r.json();
  if (data.status === "REQUEST_DENIED") return null;
  const hit = data.results?.[0]?.formatted_address;
  return hit ? trimCountry(hit) : null;
}

async function viaNominatim(lat, lon) {
  const u =
    `https://nominatim.openstreetmap.org/reverse?format=jsonv2` +
    `&lat=${lat}&lon=${lon}&accept-language=sv&zoom=18`;
  const r = await fetch(u, { headers: { "User-Agent": "Hikaya/1.0" } });
  if (!r.ok) return null;
  const data = await r.json();
  const a = data.address ?? {};
  const road = [a.road ?? a.pedestrian ?? a.footway, a.house_number]
    .filter(Boolean).join(" ");
  const town = a.village ?? a.town ?? a.city ?? a.municipality ?? "";
  const parts = [road, town].filter(Boolean);
  if (parts.length) return parts.join(", ");
  return data.display_name ? trimCountry(data.display_name) : null;
}

export default async function handler(req) {
  if (!(await validSession(req))) {
    return new Response(JSON.stringify({ error: "inte inloggad" }), { status: 401 });
  }

  const url = new URL(req.url);
  const lat = parseFloat(url.searchParams.get("lat") ?? "");
  const lon = parseFloat(url.searchParams.get("lon") ?? "");
  if (!Number.isFinite(lat) || !Number.isFinite(lon)) {
    return new Response(JSON.stringify({ error: "lat/lon krävs" }), { status: 400 });
  }

  const key = process.env.GOOGLE_MAPS_API_KEY;
  let address = null;
  if (key) {
    try { address = await viaGoogle(lat, lon, key); } catch { /* provas nedan */ }
  }
  if (!address) {
    try { address = await viaNominatim(lat, lon); } catch { /* svaret blir null */ }
  }

  return new Response(JSON.stringify({ address }), {
    headers: { "Content-Type": "application/json" },
  });
}
