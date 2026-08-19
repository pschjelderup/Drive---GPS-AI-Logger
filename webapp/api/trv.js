// Trafikverket-proxyn: webblasaren skickar sin QUERY-XML hit, nyckeln laggs
// pa har ur Vercels miljovariabler och lamnar aldrig servern. Edge-runtime,
// eftersom svaren ar tiotals megabyte och maste strommas igenom - den vanliga
// funktionsrutan har ett svarstak som NVDB sprangt for lange sedan.
//
// Bara inloggade slapps fram: utan giltig Supabase-session vore det har en
// oppen kran pa anvandarens API-kvot for vem som helst som hittar adressen.
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
  if (req.method !== "POST") {
    return new Response(JSON.stringify({ error: "bara POST" }), { status: 405 });
  }
  if (!(await validSession(req))) {
    return new Response(JSON.stringify({ error: "inte inloggad" }), { status: 401 });
  }

  const key = process.env.TRAFIKVERKET_API_KEY;
  if (!key) {
    return new Response(
      JSON.stringify({ error: "TRAFIKVERKET_API_KEY saknas i Vercels miljövariabler" }),
      { status: 500 },
    );
  }

  const query = await req.text();
  const upstream = await fetch("https://api.trafikinfo.trafikverket.se/v2/data.json", {
    method: "POST",
    headers: { "Content-Type": "text/xml" },
    body: `<REQUEST><LOGIN authenticationkey="${key}"/>${query}</REQUEST>`,
  });

  return new Response(upstream.body, {
    status: upstream.status,
    headers: { "Content-Type": "application/json" },
  });
}
