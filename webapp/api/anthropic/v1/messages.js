// Anthropic-proxyn: SDK:t i webblasaren pekas mot /api/anthropic och
// skickar sina anrop till /v1/messages - som ar exakt den har filen.
// Nyckeln byts in ur Vercels miljovariabler; den ligger inte i nagon
// webblasare. Edge-runtime sa att den strommande analysen passerar
// oforandrad.
//
// Fast sokvag, inte catch-all: [...path]-filen byggdes aldrig av Vercel
// (alla andra funktioner fanns, den gav 404), och SDK:t anvander anda
// bara den har endpointen.
//
// Bara inloggade slapps fram: en oppen proxy hade latit vem som helst
// branna anvandarens Anthropic-kredit.
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

  const headers = new Headers(req.headers);
  headers.set("x-api-key", key);
  headers.delete("authorization");
  headers.delete("host");
  headers.delete("content-length");

  const upstream = await fetch(`https://api.anthropic.com/v1/messages${url.search}`, {
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
