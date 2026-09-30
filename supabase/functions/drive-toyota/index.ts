// drive-toyota: bilen som egen kalla, via Toyotas moln.
//
// Toyotor med Toyota Connected Services laddar sjalva upp sina resor -
// start, mal, stracka, tid, forbrukning, korpoang och hela rutten - till
// Toyota. Den har funktionen hamtar dem darifran och bokfor dem som resor i
// journalen, pa samma satt som en Hikaya-enhets resor: en rad i drive_trips
// och en gpx-fil i hinken. I appen ar kopplingen en "enhet" bland de andra
// (drive_devices, kind = 'toyota'), knuten till en av flottans bilar.
//
// Sakerheten:
//  - Bara inloggade Hikaya-anvandare kommer in; JWT:n kontrolleras mot
//    Supabase Auth innan nagot annat hander.
//  - Toyota-losenordet anvands en gang, till inloggningen, och sparas
//    aldrig. Det som sparas ar Toyotas token, i drive_connector_accounts -
//    en tabell utan en enda RLS-policy, sa att webblasaren aldrig kan lasa
//    den. Bara den har funktionen (service-rollen) nar den.
//
// Vagar (POST, JSON):
//   {action:"connect", username, password, brand}  logga in, lista bilarna
//   {action:"vehicles", account_id}                  lista bilarna igen
//   {action:"link", account_id, vin, vehicle_id?, new_vehicle?, name?}
//   {action:"unlink", device_id}
//   {action:"sync", device_id, from?, to?, cursor?}  hamta resor och status
//
// Synken ar uppdelad i korta varv: hogst ett par sidor resor per anrop, och
// ett cursor-varde tillbaka nar det finns mer. Webbappen anropar igen tills
// cursor ar null. Da hinner inget anrop sla i funktionens tidsgranser, och en
// lang historik fylls pa i lugn takt.
import "jsr:@supabase/functions-js/edge-runtime.d.ts";
import { createClient } from "jsr:@supabase/supabase-js@2";
import * as toyota from "./toyota.ts";

const supabase = createClient(
  Deno.env.get("SUPABASE_URL")!,
  Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!,
);

const BUCKET_GPX = "drive-gpx";
const PAGE = 10;           // resor per sida fran Toyota (med rutt)
const PAGES_PER_CALL = 2;  // sidor per anrop
const FIRST_SYNC_DAYS = 60;

const CORS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, apikey, content-type, x-client-info",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
};

function json(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { ...CORS, "Content-Type": "application/json" },
  });
}

async function logSync(deviceId: string, action: string, detail: string | null, antal: number | null) {
  try {
    await supabase.from("drive_sync_log").insert({ device_id: deviceId, action, detail, antal });
  } catch { /* historiken ar hjalpmedel */ }
}

function isoDate(d: Date): string {
  return d.toISOString().slice(0, 10);
}

// ---- kontot och token -------------------------------------------------------

type Account = {
  id: number;
  brand: toyota.Brand;
  username: string;
  uuid: string | null;
  access_token: string | null;
  access_expires: string | null;
  refresh_token: string | null;
};

async function saveTokens(accountId: number, t: toyota.Tokens) {
  await supabase.from("drive_connector_accounts").update({
    uuid: t.uuid,
    access_token: t.access_token,
    access_expires: new Date(t.expires_at).toISOString(),
    refresh_token: t.refresh_token,
    needs_login: false,
    updated_at: new Date().toISOString(),
  }).eq("id", accountId);
}

// Giltig token for kontot: den sparade om den lever, annars en fornyad.
async function tokensFor(acc: Account): Promise<toyota.Tokens> {
  const exp = acc.access_expires ? Date.parse(acc.access_expires) : 0;
  if (acc.access_token && acc.uuid && acc.refresh_token && exp > Date.now()) {
    return {
      access_token: acc.access_token,
      refresh_token: acc.refresh_token,
      uuid: acc.uuid,
      expires_at: exp,
    };
  }
  if (!acc.refresh_token) {
    throw new toyota.ToyotaError("reauth", "logga in på Toyota-kontot igen");
  }
  try {
    const t = await toyota.refresh(acc.refresh_token);
    await saveTokens(acc.id, t);
    return t;
  } catch (e) {
    if (e instanceof toyota.ToyotaError && e.kind === "reauth") {
      await supabase.from("drive_connector_accounts")
        .update({ needs_login: true }).eq("id", acc.id);
    }
    throw e;
  }
}

async function loadAccount(id: number): Promise<Account | null> {
  const { data } = await supabase.from("drive_connector_accounts")
    .select("id, brand, username, uuid, access_token, access_expires, refresh_token")
    .eq("id", id).maybeSingle();
  return (data as Account) ?? null;
}

// Ett las per konto: Toyota byter refresh-token vid varje fornyelse, och
// tva samtidiga synkar skulle darfor sla ut varandra. Laset gar ut av sig
// sjalvt efter tva minuter om en funktion dor mitt i.
async function takeLock(id: number): Promise<boolean> {
  const now = new Date();
  const { data } = await supabase.from("drive_connector_accounts")
    .update({ lock_until: new Date(now.getTime() + 120000).toISOString() })
    .eq("id", id)
    .or(`lock_until.is.null,lock_until.lt.${now.toISOString()}`)
    .select("id");
  return !!data?.length;
}

async function releaseLock(id: number) {
  await supabase.from("drive_connector_accounts").update({ lock_until: null }).eq("id", id);
}

// ---- resorna ----------------------------------------------------------------

function xmlEsc(s: string): string {
  return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

// Rutten som gpx. Toyotas ruttpunkter bar ingen tid, sa ingen tid skrivs -
// fartlagret pa kartan visar da "vet ej" i stallet for en pahittad fart.
function routeGpx(name: string, route: Array<Record<string, unknown>>): string | null {
  const pts = route.filter((p) =>
    Number.isFinite(p.lat as number) && Number.isFinite(p.lon as number));
  if (pts.length < 2) return null;
  const body = pts.map((p) =>
    `<trkpt lat="${(p.lat as number).toFixed(6)}" lon="${(p.lon as number).toFixed(6)}"/>`,
  ).join("\n");
  return `<?xml version="1.0" encoding="UTF-8"?>
<gpx version="1.1" creator="Hikaya via Toyota Connected Services" xmlns="http://www.topografix.com/GPX/1/1">
<trk><name>${xmlEsc(name)}</name><trkseg>
${body}
</trkseg></trk>
</gpx>
`;
}

function num(v: unknown): number | null {
  return typeof v === "number" && Number.isFinite(v) ? v : null;
}

// En Toyota-resa -> en rad i drive_trips. Rutten ligger i gpx-filen, resten
// av Toyotas svar foljer med som raw.
function tripRow(t: Record<string, unknown>, deviceId: string, vehicleId: number | null, tripNo: number) {
  const s = (t.summary ?? {}) as Record<string, unknown>;
  const scores = (t.scores ?? {}) as Record<string, unknown>;
  const hdc = (t.hdc ?? null) as Record<string, unknown> | null;
  const duration = num(s.duration);
  const idle = num(s.durationIdle) ?? 0;
  const fuelMl = num(s.fuelConsumption);
  const { route: _route, ...raw } = t;

  // Bilens egna matvarden i samma falt som OBD-adapterns - journalen visar
  // dem da pa samma plats. Bara det Toyota faktiskt lamnat skrivs.
  const obd: Record<string, unknown> = { kalla: "toyota" };
  if (fuelMl != null) obd.bransle_l = Math.round(fuelMl) / 1000;
  if (idle > 0) obd.tomgang_s = idle;
  if (hdc) {
    const evM = num(hdc.evDistance);
    const evS = num(hdc.evTime);
    if (evM != null) obd.el_km = Math.round(evM / 10) / 100;
    if (evS != null) obd.el_s = evS;
  }

  return {
    device_id: deviceId,
    trip_no: tripNo,
    ext_id: String(t.id),
    vehicle_id: vehicleId,
    start_utc: (s.startTs as string) ?? null,
    end_utc: (s.endTs as string) ?? null,
    start_lat: num(s.startLat),
    start_lon: num(s.startLon),
    end_lat: num(s.endLat),
    end_lon: num(s.endLon),
    distance_m: num(s.length) ?? 0,
    points: Array.isArray(t.route) ? t.route.length : 0,
    // Ingen har svarat pa fragan om syftet - resan ar omarkt tills nagon
    // markerar den i journalen, eller en egen plats gor det.
    purpose: "omarkt",
    max_speed_kmh: num(s.maxSpeed),
    speeding_s: num(s.durationOverspeed),
    moving_s: duration != null ? Math.max(0, duration - idle) : null,
    eco_score: num(scores.global),
    hard_events: null,
    end_reason: "toyota",
    obd,
    // Toyotas rutt borjar dar bilen startade - det finns ingen kallstartslucka
    // att fylla, och luckfyllnaden ska inte leta efter en.
    gap_filled_m: 0,
    raw,
  };
}

async function syncTrips(
  device: Record<string, unknown>, tok: toyota.Tokens, brand: toyota.Brand,
  from: string, to: string, offset: number,
): Promise<{ inserted: number; next: number | null }> {
  const deviceId = String(device.id);
  const conn = (device.connector ?? {}) as Record<string, unknown>;
  const vin = String(conn.vin);
  const vehicleId = (device.vehicle_id as number) ?? null;

  const { data: top } = await supabase.from("drive_trips").select("trip_no")
    .eq("device_id", deviceId).order("trip_no", { ascending: false }).limit(1);
  let tripNo = (top?.[0]?.trip_no as number) ?? 0;

  let inserted = 0;
  let next: number | null = offset;
  for (let page = 0; page < PAGES_PER_CALL && next != null; page++) {
    const res = await toyota.tripsPage(tok, vin, from, to, next, PAGE, brand);
    next = res.next;
    const trips = res.trips.filter((t) => t.id);
    if (!trips.length) continue;

    const ids = trips.map((t) => String(t.id));
    const { data: have } = await supabase.from("drive_trips").select("ext_id")
      .eq("device_id", deviceId).in("ext_id", ids);
    const known = new Set((have ?? []).map((r) => r.ext_id));

    // Aldst forst, sa att resenumren foljer tiden inom en sida.
    const fresh = trips.filter((t) => !known.has(String(t.id))).reverse();
    for (const t of fresh) {
      tripNo++;
      const row = tripRow(t, deviceId, vehicleId, tripNo);
      const gpxName = `R${String(tripNo).padStart(4, "0")}.GPX`;
      const xml = routeGpx(`${device.name ?? deviceId} ${row.start_utc ?? ""}`,
        (t.route ?? []) as Array<Record<string, unknown>>);
      let gpxPath: string | null = null;
      if (xml) {
        const path = `${deviceId}/${gpxName}`;
        const { error } = await supabase.storage.from(BUCKET_GPX).upload(
          path, new TextEncoder().encode(xml),
          { upsert: true, contentType: "application/gpx+xml" },
        );
        if (!error) gpxPath = path;
      }
      const { error } = await supabase.from("drive_trips").insert({
        ...row,
        gpx_name: gpxPath ? gpxName : null,
        gpx_path: gpxPath,
      });
      if (error) throw new Error(`resan kunde inte sparas: ${error.message}`);
      inserted++;
    }
  }

  if (inserted) {
    await supabase.from("drive_devices").update({ last_synced_trip: tripNo }).eq("id", deviceId);
  }
  return { inserted, next };
}

// Bilens lage just nu: matarstallning, tank, rackvidd och var den star. Hamtas
// en gang per synk; matarstallningen blir dessutom en avstamning i
// drive_odometer, sa att journalens matarstallning foljer bilens egen.
async function syncStatus(
  device: Record<string, unknown>, tok: toyota.Tokens, brand: toyota.Brand,
): Promise<Record<string, unknown>> {
  const conn = (device.connector ?? {}) as Record<string, unknown>;
  const vin = String(conn.vin);
  const out: Record<string, unknown> = {};

  try {
    const tel = await toyota.telemetry(tok, vin, brand);
    const odo = (tel.odometer ?? null) as { value?: number; unit?: string } | null;
    if (odo && typeof odo.value === "number") {
      const km = odo.unit === "mi" ? odo.value * 1.609344 : odo.value;
      out.odometer_km = Math.round(km * 10) / 10;
    }
    if (typeof tel.fuelLevel === "number") out.fuel_pct = tel.fuelLevel;
    if (typeof tel.batteryLevel === "number") out.battery_pct = tel.batteryLevel;
    const dte = (tel.distanceToEmpty ?? null) as { value?: number; unit?: string } | null;
    if (dte && typeof dte.value === "number") {
      out.range_km = Math.round(dte.unit === "mi" ? dte.value * 1.609344 : dte.value);
    }
    if (tel.timestamp) out.telemetry_at = tel.timestamp;
  } catch (e) {
    out.telemetry_error = e instanceof Error ? e.message : String(e);
  }

  try {
    const loc = await toyota.location(tok, vin, brand);
    const v = (loc.vehicleLocation ?? null) as Record<string, unknown> | null;
    if (v && typeof v.latitude === "number" && typeof v.longitude === "number") {
      out.lat = v.latitude;
      out.lon = v.longitude;
      out.located_at = v.locationAcquisitionDatetime ?? loc.lastTimestamp ?? null;
    }
  } catch { /* positionen ar valfri - alla bilar lamnar den inte */ }

  // Avstamningen: en ny rad nar stallningen andrats med minst en kilometer
  // sedan Toyotas forra, sa att tabellen inte fylls av samma siffra.
  const vehicleId = (device.vehicle_id as number) ?? null;
  if (vehicleId && typeof out.odometer_km === "number") {
    const { data: last } = await supabase.from("drive_odometer")
      .select("odometer_km").eq("vehicle_id", vehicleId).eq("note", "Toyota")
      .order("read_at", { ascending: false }).limit(1);
    const prev = last?.[0]?.odometer_km as number | undefined;
    if (prev == null || Math.abs((out.odometer_km as number) - prev) >= 1) {
      await supabase.from("drive_odometer").insert({
        vehicle_id: vehicleId,
        odometer_km: out.odometer_km,
        read_at: (out.telemetry_at as string) ?? new Date().toISOString(),
        note: "Toyota",
      });
    }
  }
  return out;
}

// ---- vagarna ----------------------------------------------------------------

async function handle(body: Record<string, unknown>): Promise<Response> {
  const action = String(body.action ?? "");

  if (action === "connect") {
    const username = String(body.username ?? "").trim().toLowerCase();
    const password = String(body.password ?? "");
    const brand: toyota.Brand = body.brand === "L" ? "L" : "T";
    if (!username || !password) return json({ error: "e-post och lösenord behövs" }, 400);

    const t = await toyota.login(username, password);
    const { data: acc, error } = await supabase.from("drive_connector_accounts")
      .upsert({ provider: "toyota", brand, username }, { onConflict: "provider,username" })
      .select("id").single();
    if (error || !acc) return json({ error: error?.message ?? "kontot kunde inte sparas" }, 500);
    await saveTokens(acc.id, t);

    const list = await toyota.vehicles(t, brand);
    // Kopplade bilar pa samma konto far sin felrad nollstalld - det ar sa
    // "logga in igen" fungerar: nasta synk tar den nya token.
    const { data: linked } = await supabase.from("drive_devices")
      .select("id, connector").eq("kind", "toyota")
      .eq("connector->>account_id", String(acc.id));
    for (const d of linked ?? []) {
      await supabase.from("drive_devices").update({
        connector: { ...(d.connector ?? {}), needs_login: false, last_error: null },
      }).eq("id", d.id);
    }
    return json({ account_id: acc.id, vehicles: list });
  }

  if (action === "vehicles") {
    const acc = await loadAccount(Number(body.account_id));
    if (!acc) return json({ error: "okänt konto" }, 404);
    const t = await tokensFor(acc);
    return json({ account_id: acc.id, vehicles: await toyota.vehicles(t, acc.brand) });
  }

  if (action === "link") {
    const acc = await loadAccount(Number(body.account_id));
    if (!acc) return json({ error: "okänt konto" }, 404);
    const vin = String(body.vin ?? "").toUpperCase();
    if (!/^[A-HJ-NPR-Z0-9]{17}$/.test(vin)) return json({ error: "ogiltigt VIN" }, 400);
    const t = await tokensFor(acc);
    const car = (await toyota.vehicles(t, acc.brand)).find((v) => v.vin === vin);
    if (!car) return json({ error: "bilen finns inte på Toyota-kontot" }, 404);

    let vehicleId = body.vehicle_id ? Number(body.vehicle_id) : null;
    if (!vehicleId && body.new_vehicle) {
      const { data: v, error } = await supabase.from("drive_vehicles").insert({
        name: car.nickname || car.model || "Toyota",
        regnr: car.regnr,
      }).select("id").single();
      if (error || !v) return json({ error: error?.message ?? "bilen kunde inte skapas" }, 500);
      vehicleId = v.id;
    }

    const id = `toyota-${vin}`;
    const token = Array.from(crypto.getRandomValues(new Uint8Array(24)))
      .map((b) => b.toString(16).padStart(2, "0")).join("");
    const connector = {
      provider: "toyota",
      account_id: acc.id,
      username: acc.username,
      brand: acc.brand,
      vin,
      model: car.model,
      year: car.year,
      regnr: car.regnr,
      fuel: car.fuel,
      image: car.image,
    };
    const { data: existing } = await supabase.from("drive_devices")
      .select("id").eq("id", id).maybeSingle();
    const { error } = existing
      ? await supabase.from("drive_devices").update({
          vehicle_id: vehicleId, connector, kind: "toyota",
          name: String(body.name ?? "") || car.nickname || car.model || vin,
        }).eq("id", id)
      : await supabase.from("drive_devices").insert({
          id, kind: "toyota", token, vehicle_id: vehicleId, connector,
          // Token ar oanvand - kopplingen synkar via den har funktionen,
          // inte via drive-sync - men kolumnen kraver en, och en slumpad
          // som aldrig visas oppnar ingenting.
          name: String(body.name ?? "") || car.nickname || car.model || vin,
        });
    if (error) return json({ error: error.message }, 500);
    await logSync(id, "koppling", `Toyota ${car.model ?? ""} ${car.regnr ?? ""}`.trim(), null);
    return json({ device_id: id, vehicle_id: vehicleId });
  }

  if (action === "unlink") {
    const id = String(body.device_id ?? "");
    const { data: d } = await supabase.from("drive_devices")
      .select("id, kind, connector").eq("id", id).maybeSingle();
    if (!d || d.kind !== "toyota") return json({ error: "okänd koppling" }, 404);
    await supabase.from("drive_devices").delete().eq("id", id);
    // Sista bilen pa kontot: da slangs aven Toyotas token.
    const accId = (d.connector as Record<string, unknown>)?.account_id;
    const { count } = await supabase.from("drive_devices")
      .select("id", { count: "exact", head: true })
      .eq("kind", "toyota").eq("connector->>account_id", String(accId));
    if (accId && !count) {
      await supabase.from("drive_connector_accounts").delete().eq("id", accId);
    }
    return json({ ok: true });
  }

  if (action === "sync") {
    const id = String(body.device_id ?? "");
    const { data: device } = await supabase.from("drive_devices")
      .select("*").eq("id", id).maybeSingle();
    if (!device || device.kind !== "toyota") return json({ error: "okänd koppling" }, 404);
    const conn = (device.connector ?? {}) as Record<string, unknown>;
    const acc = await loadAccount(Number(conn.account_id));
    if (!acc) return json({ error: "Toyota-kontot saknas – logga in igen", needs_login: true }, 409);

    if (!(await takeLock(acc.id))) return json({ busy: true, next: body.cursor ?? null });
    try {
      const t = await tokensFor(acc);

      // Fonstret: angivet av webbappen (historik), annars fran dagen fore
      // senaste kanda resa till idag. Forsta gangen de senaste 60 dagarna.
      const cursor = (body.cursor ?? null) as { from: string; to: string; offset: number } | null;
      let from = cursor?.from ?? (body.from as string | undefined);
      const to = cursor?.to ?? (body.to as string | undefined) ?? isoDate(new Date());
      if (!from) {
        const { data: last } = await supabase.from("drive_trips").select("start_utc")
          .eq("device_id", id).not("start_utc", "is", null)
          .order("start_utc", { ascending: false }).limit(1);
        const d = last?.[0]?.start_utc
          ? new Date(Date.parse(last[0].start_utc as string) - 86400000)
          : new Date(Date.now() - FIRST_SYNC_DAYS * 86400000);
        from = isoDate(d);
      }
      if (!/^\d{4}-\d{2}-\d{2}$/.test(from) || !/^\d{4}-\d{2}-\d{2}$/.test(to)) {
        return json({ error: "ogiltigt datum" }, 400);
      }

      // Bilens lage bara i forsta varvet - fortsattningsvarven ar resor.
      let status: Record<string, unknown> | null = null;
      if (!cursor) status = await syncStatus(device, t, acc.brand);

      const r = await syncTrips(device, t, acc.brand, from, to, cursor?.offset ?? 0);
      const next = r.next != null ? { from, to, offset: r.next } : null;

      const connector = {
        ...conn,
        ...(status ?? {}),
        last_sync: new Date().toISOString(),
        last_error: null,
        needs_login: false,
      };
      await supabase.from("drive_devices").update({
        connector, last_seen: new Date().toISOString(),
      }).eq("id", id);
      if (r.inserted) await logSync(id, "resor", `Toyota ${from} – ${to}`, r.inserted);
      return json({ inserted: r.inserted, next, status });
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      const needsLogin = e instanceof toyota.ToyotaError && e.kind === "reauth";
      await supabase.from("drive_devices").update({
        connector: { ...conn, last_error: msg, needs_login: needsLogin },
      }).eq("id", id);
      await logSync(id, "fel", msg.slice(0, 200), null);
      return json({ error: msg, needs_login: needsLogin }, needsLogin ? 409 : 502);
    } finally {
      await releaseLock(acc.id);
    }
  }

  return json({ error: "okänd åtgärd" }, 404);
}

Deno.serve(async (req: Request) => {
  if (req.method === "OPTIONS") return new Response(null, { headers: CORS });
  if (req.method !== "POST") return json({ error: "bara POST" }, 405);

  // Bara inloggade: JWT:n prövas mot Supabase Auth.
  const auth = req.headers.get("authorization") ?? "";
  const jwt = auth.startsWith("Bearer ") ? auth.slice(7) : "";
  const { data: who } = jwt ? await supabase.auth.getUser(jwt) : { data: null };
  if (!who?.user) return json({ error: "inte inloggad" }, 401);

  let body: Record<string, unknown>;
  try {
    body = await req.json();
  } catch {
    return json({ error: "ogiltig json" }, 400);
  }
  try {
    return await handle(body);
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    const kind = e instanceof toyota.ToyotaError ? e.kind : "fel";
    // 400, inte 401: ett fel Toyota-losenord ar inte samma sak som att
    // Hikaya-inloggningen gatt ut, och appen ska kunna skilja dem at.
    const status = kind === "login" ? 400 : kind === "reauth" ? 409 : 502;
    return json({ error: msg, needs_login: kind === "reauth" }, status);
  }
});
