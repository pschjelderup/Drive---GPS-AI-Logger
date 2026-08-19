// Anthropic-proxyn: SDK:t i webblasaren pekas hit i stallet for mot
// api.anthropic.com, och nyckeln byts in har ur Vercels miljovariabler -
// den ligger inte langre i nagon webblasare. Edge-runtime sa att den
// strommande analysen passerar oforandrad.
//
// Bara inloggade slapps fram: en oppen proxy hade latit vem som helst brenna
// anvandarens Anthropic-kredit.
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

  const key = process.env.ANTHROPIC_API_KEY;
  if (!key) {
    return new Response(
      JSON.stringify({ error: "ANTHROPIC_API_KEY saknas i Vercels miljövariabler" }),
      { status: 500 },
    );
  }

  const url = new URL(req.url);
  const path = url.pathname.replace(/^\/api\/anthropic/, "");

  const headers = new Headers(req.headers);
  headers.set("x-api-key", key);
  headers.delete("authorization");
  headers.delete("host");
  headers.delete("content-length");

  const upstream = await fetch(`https://api.anthropic.com${path}${url.search}`, {
    method: req.method,
    headers,
    body: req.body,
    duplex: "half",
  });

  return new Response(upstream.body, {
    status: upstream.status,
    headers: upstream.headers,
  });
}
