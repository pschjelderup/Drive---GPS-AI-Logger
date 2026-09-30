// Toyota Connected Services Europe - klienten.
//
// Samma vag som MyToyota-appen och det oppna biblioteket pytoyoda
// (github.com/pytoyoda/pytoyoda, MIT) gar: ForgeRock-inloggning mot
// b2c-login.toyota-europe.com, en OAuth-kod, en token, och sedan
// "one app"-API:t med ett fast antal huvuden. Konstanterna nedan ar
// appens egna publika varden, publicerade i biblioteket - inga hemligheter
// som hor till oss. Det hemliga ar anvandarens token, och den lamnar
// aldrig molnfunktionen.
//
// Ren fetch och Web Crypto, inga beroenden.

const API_BASE = "https://ctpa-oneapi.tceu-ctp-prd.toyotaconnectedeurope.io";
const CLIENT_VERSION = "2.14.0";
const API_KEY = "tTZipv6liF74PwMfk9Ed68AQ0bISswwf3iHQdqcF";

const AUTH_BASE = "https://b2c-login.toyota-europe.com";
const REALM = `${AUTH_BASE}/json/realms/root/realms/tme`;
const OAUTH = `${AUTH_BASE}/oauth2/realms/root/realms/tme`;
const CLIENT_ID = "oneapp";
const REDIRECT_URI = "com.toyota.oneapp:/oauth2Callback";
const BASIC_AUTH = "basic b25lYXBwOm9uZWFwcA==";

export type Brand = "T" | "L";

export class ToyotaError extends Error {
  // "login" = kontot eller losenordet, "reauth" = token dog och ett nytt
  // losenord behovs, "api" = Toyota svarade fel, "rate" = for manga anrop.
  kind: "login" | "reauth" | "api" | "rate";
  constructor(kind: "login" | "reauth" | "api" | "rate", msg: string) {
    super(msg);
    this.kind = kind;
  }
}

export type Tokens = {
  access_token: string;
  refresh_token: string;
  uuid: string;
  expires_at: number; // ms
};

function b64urlJson(part: string): Record<string, unknown> {
  const pad = part.replace(/-/g, "+").replace(/_/g, "/");
  const bin = atob(pad + "=".repeat((4 - (pad.length % 4)) % 4));
  const bytes = Uint8Array.from(bin, (c) => c.charCodeAt(0));
  return JSON.parse(new TextDecoder().decode(bytes));
}

function tokensFrom(body: Record<string, unknown>): Tokens {
  for (const k of ["access_token", "id_token", "refresh_token", "expires_in"]) {
    if (!(k in body)) throw new ToyotaError("login", `tokensvaret saknar ${k}`);
  }
  const claims = b64urlJson(String(body.id_token).split(".")[1]);
  const uuid = String(claims.uuid ?? "");
  if (!uuid) throw new ToyotaError("login", "tokensvaret saknar uuid");
  return {
    access_token: String(body.access_token),
    refresh_token: String(body.refresh_token),
    uuid,
    // En minut marginal, sa att en token aldrig gar ut mitt i en runda.
    expires_at: Date.now() + (Number(body.expires_in) - 60) * 1000,
  };
}

type Callback = {
  type: string;
  output?: Array<{ name: string; value: unknown }>;
  input?: Array<{ name: string; value: unknown }>;
};

// Inloggningen: ForgeRock staller fragor (callbacks) tills den ar nojd och
// lamnar ut ett tokenId. Vi svarar pa anvandarnamn och losenord; allt annat
// - till exempel ett extra verifieringssteg - rapporteras i klartext i
// stallet for att snurra.
export async function login(username: string, password: string): Promise<Tokens> {
  const authenticate =
    `${REALM}/authenticate?authIndexType=service&authIndexValue=oneapp`;
  let data: Record<string, unknown> = {};
  let tokenId = "";
  for (let i = 0; i < 10 && !tokenId; i++) {
    for (const cb of (data.callbacks ?? []) as Callback[]) {
      const label = String(cb.output?.[0]?.value ?? "");
      if (cb.type === "NameCallback" && label === "User Name" && cb.input?.[0]) {
        cb.input[0].value = username;
      } else if (cb.type === "PasswordCallback" && cb.input?.[0]) {
        cb.input[0].value = password;
      } else if (cb.type === "TextOutputCallback" && label === "User Not Found") {
        throw new ToyotaError("login", "Toyota känner inte igen e-postadressen");
      }
    }
    const r = await fetch(authenticate, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(data),
    });
    const text = await r.text();
    if (r.status === 401) throw new ToyotaError("login", "fel e-post eller lösenord");
    if (!r.ok) {
      throw new ToyotaError("login", `inloggningen svarade ${r.status}`);
    }
    data = JSON.parse(text);
    if (typeof data.tokenId === "string") tokenId = data.tokenId;
    else if (!Array.isArray(data.callbacks)) {
      throw new ToyotaError("login", "inloggningen svarade utan frågor och utan token");
    }
  }
  if (!tokenId) {
    const types = ((data.callbacks ?? []) as Callback[]).map((c) => c.type).join(", ");
    throw new ToyotaError("login",
      `inloggningen ville ha mer än e-post och lösenord (${types}) – logga in en gång i MyToyota-appen och prova igen`);
  }

  const authorize = `${OAUTH}/authorize?client_id=${CLIENT_ID}` +
    `&scope=openid+profile+write&response_type=code` +
    `&redirect_uri=${REDIRECT_URI}&code_challenge=plain&code_challenge_method=plain`;
  const a = await fetch(authorize, {
    headers: { cookie: `iPlanetDirectoryPro=${tokenId}` },
    redirect: "manual",
  });
  const loc = a.headers.get("location") ?? "";
  if (a.status !== 302 || !loc) {
    throw new ToyotaError("login", `behörigheten svarade ${a.status}`);
  }
  // Omdirigeringen gar till appens eget schema - koden ar det enda vi vill ha.
  const code = new URL(loc.replace(/^[a-z.]+:\/?/i, "https://app/")).searchParams.get("code");
  if (!code) throw new ToyotaError("login", "behörigheten gav ingen kod");

  const t = await fetch(`${OAUTH}/access_token`, {
    method: "POST",
    headers: {
      authorization: BASIC_AUTH,
      "Content-Type": "application/x-www-form-urlencoded",
    },
    body: new URLSearchParams({
      client_id: CLIENT_ID,
      code,
      redirect_uri: REDIRECT_URI,
      grant_type: "authorization_code",
      code_verifier: "plain",
    }),
  });
  if (!t.ok) throw new ToyotaError("login", `tokenhämtningen svarade ${t.status}`);
  return tokensFrom(await t.json());
}

// Ny token ur den sparade. Toyota byter refresh-token vid varje fornyelse,
// sa den som kommer tillbaka maste sparas i stallet for den gamla.
export async function refresh(refreshToken: string): Promise<Tokens> {
  const r = await fetch(`${OAUTH}/access_token`, {
    method: "POST",
    headers: {
      authorization: BASIC_AUTH,
      "Content-Type": "application/x-www-form-urlencoded",
    },
    body: new URLSearchParams({
      client_id: CLIENT_ID,
      redirect_uri: REDIRECT_URI,
      grant_type: "refresh_token",
      code_verifier: "plain",
      refresh_token: refreshToken,
    }),
  });
  if (!r.ok) {
    throw new ToyotaError("reauth",
      "inloggningen hos Toyota har gått ut – logga in på Toyota-kontot igen");
  }
  return tokensFrom(await r.json());
}

async function hmacHex(key: string, msg: string): Promise<string> {
  const k = await crypto.subtle.importKey(
    "raw", new TextEncoder().encode(key),
    { name: "HMAC", hash: "SHA-256" }, false, ["sign"],
  );
  const sig = await crypto.subtle.sign("HMAC", k, new TextEncoder().encode(msg));
  return Array.from(new Uint8Array(sig))
    .map((b) => b.toString(16).padStart(2, "0")).join("");
}

// Ett anrop mot one app-API:t. 429 och 5xx provas om med kort vantan;
// allt annat ar ett svar.
export async function api(
  tok: Tokens, path: string, vin?: string, brand: Brand = "T",
): Promise<Record<string, unknown>> {
  const headers: Record<string, string> = {
    "x-api-key": API_KEY,
    "API_KEY": API_KEY,
    "x-guid": tok.uuid,
    "guid": tok.uuid,
    "x-client-ref": await hmacHex(CLIENT_VERSION, tok.uuid),
    "x-correlationid": crypto.randomUUID(),
    "x-appversion": CLIENT_VERSION,
    "x-channel": "ONEAPP",
    "x-brand": brand,
    "x-region": "EU",
    "x-user-region": "EU",
    "authorization": `Bearer ${tok.access_token}`,
    "user-agent": "okhttp/4.10.0",
  };
  if (brand === "L") {
    headers["x-appbrand"] = "L";
    headers["brand"] = "L";
  }
  if (vin) headers["vin"] = vin;

  const route = path.split("?")[0];
  for (const wait of [0, 2000, 4000]) {
    if (wait) await new Promise((res) => setTimeout(res, wait));
    const r = await fetch(API_BASE + path, { headers });
    if (r.status === 429 || r.status >= 500) continue;
    const text = await r.text();
    if (r.status === 401) throw new ToyotaError("reauth", "Toyota godtog inte inloggningen");
    if (!r.ok) throw new ToyotaError("api", `${route} svarade ${r.status}`);
    return text ? JSON.parse(text) : {};
  }
  throw new ToyotaError("rate", `${route}: Toyota svarar inte just nu – prova om en stund`);
}

// ---- de anrop vi anvander ------------------------------------------------

export type Vehicle = {
  vin: string;
  nickname: string | null;
  regnr: string | null;
  model: string | null;
  year: string | null;
  fuel: string | null;
  image: string | null;
};

export async function vehicles(tok: Tokens, brand: Brand = "T"): Promise<Vehicle[]> {
  const body = await api(tok, "/v2/vehicle/guid", undefined, brand);
  const list = (body.payload ?? []) as Array<Record<string, unknown>>;
  return list.filter((v) => v.vin).map((v) => ({
    vin: String(v.vin),
    nickname: (v.nickName as string) || null,
    regnr: (v.registrationNumber as string) || null,
    model: (v.modelName as string) || (v.displayModelDescription as string) || null,
    year: (v.modelYear as string) || null,
    fuel: (v.fuelType as string) || null,
    image: (v.image as string) || null,
  }));
}

// Resor mellan tva datum (YYYY-MM-DD, bada inklusive), nyast forst, en sida
// i taget. next ar null pa sista sidan.
export async function tripsPage(
  tok: Tokens, vin: string, from: string, to: string, offset: number,
  limit: number, brand: Brand = "T",
): Promise<{ trips: Array<Record<string, unknown>>; next: number | null }> {
  const q = `/v1/trips?from=${from}&to=${to}&route=true&summary=false` +
    `&limit=${limit}&offset=${offset}`;
  const body = await api(tok, q, vin, brand);
  const p = (body.payload ?? {}) as Record<string, unknown>;
  const trips = (p.trips ?? []) as Array<Record<string, unknown>>;
  const meta = (p._metadata ?? {}) as Record<string, Record<string, unknown>>;
  const next = meta.pagination?.nextOffset;
  return { trips, next: typeof next === "number" ? next : null };
}

export async function telemetry(tok: Tokens, vin: string, brand: Brand = "T") {
  const body = await api(tok, "/v3/telemetry", vin, brand);
  return (body.payload ?? {}) as Record<string, unknown>;
}

export async function location(tok: Tokens, vin: string, brand: Brand = "T") {
  const body = await api(tok, "/v1/location", vin, brand);
  return (body.payload ?? {}) as Record<string, unknown>;
}
