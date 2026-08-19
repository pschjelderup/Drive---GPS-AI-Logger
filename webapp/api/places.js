// Google Places-proxyn: slar upp platser nara en koordinat at journalen.
// Nyckeln ligger i Vercels miljovariabler (GOOGLE_MAPS_API_KEY) och lamnar
// aldrig servern. Bara inloggade slapps fram, av samma skal som i de andra
// proxyerna: en oppen kran pa ett betalt API ar en rakning som vaxer.
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

export default async function handler(req) {
  if (!(await validSession(req))) {
    return new Response(JSON.stringify({ error: "inte inloggad" }), { status: 401 });
  }

  const key = process.env.GOOGLE_MAPS_API_KEY;
  if (!key) {
    return new Response(
      JSON.stringify({ error: "GOOGLE_MAPS_API_KEY saknas i Vercels miljövariabler" }),
      { status: 500 },
    );
  }

  const url = new URL(req.url);
  const lat = parseFloat(url.searchParams.get("lat") ?? "");
  const lon = parseFloat(url.searchParams.get("lon") ?? "");
  const hasAnchor = Number.isFinite(lat) && Number.isFinite(lon);
  const qRaw = (url.searchParams.get("q") ?? "").trim();
  if (!hasAnchor && !qRaw) {
    return new Response(
      JSON.stringify({ error: "lat/lon eller söktext krävs" }), { status: 400 });
  }

  // Tva lagen: utan soktext ar det narmaste-forst inom 600 m - langre an sa
  // ar det inte platsen resan startade pa. Med soktext ar det fritextsok med
  // dragning mot positionen, sa "ICA Maxi" hittar ratt butik och inte en i
  // en annan stad.
  const q = qRaw;
  const endpoint = q ? "searchText" : "searchNearby";
  const body = q
    ? {
        textQuery: q,
        pageSize: 10,
        languageCode: "sv",
        // Med ankare dras traffarna mot positionen; utan ankare - som nar en
        // kunds kontor soks upp pa namn - halls soket inom Sverige.
        locationBias: hasAnchor
          ? { circle: { center: { latitude: lat, longitude: lon }, radius: 5000 } }
          : { rectangle: {
              low: { latitude: 55.0, longitude: 10.5 },
              high: { latitude: 69.1, longitude: 24.2 },
            } },
      }
    : {
        maxResultCount: 15,
        rankPreference: "DISTANCE",
        languageCode: "sv",
        locationRestriction: {
          circle: { center: { latitude: lat, longitude: lon }, radius: 600 },
        },
      };

  const upstream = await fetch(`https://places.googleapis.com/v1/places:${endpoint}`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "X-Goog-Api-Key": key,
      "X-Goog-FieldMask":
        "places.displayName,places.formattedAddress,places.location",
    },
    body: JSON.stringify(body),
  });

  if (!upstream.ok) {
    const text = await upstream.text();
    return new Response(
      JSON.stringify({ error: `Places svarade ${upstream.status}: ${text.slice(0, 200)}` }),
      { status: 502 },
    );
  }

  const data = await upstream.json();
  const R = 6371000;
  const rad = Math.PI / 180;
  const places = (data.places ?? []).map((p) => {
    const pla = p.location?.latitude ?? null;
    const plo = p.location?.longitude ?? null;
    let dist = -1;
    if (hasAnchor && pla != null) {
      const dLat = (pla - lat) * rad;
      const dLon = (plo - lon) * rad;
      const s = Math.sin(dLat / 2) ** 2 +
        Math.cos(lat * rad) * Math.cos(pla * rad) * Math.sin(dLon / 2) ** 2;
      dist = Math.round(2 * R * Math.asin(Math.sqrt(s)));
    }
    return {
      name: p.displayName?.text ?? "okänd plats",
      address: p.formattedAddress ?? "",
      lat: pla,
      lon: plo,
      distance_m: dist,
    };
  });

  return new Response(JSON.stringify({ places }), {
    headers: { "Content-Type": "application/json" },
  });
}
