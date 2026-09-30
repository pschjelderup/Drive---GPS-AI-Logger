// Toyota-kopplingen fran webbappens sida. Allt gar via molnfunktionen
// drive-toyota: den loggar in hos Toyota, haller Toyotas token och hamtar
// resorna. Webblasaren ser aldrig nagon token, och losenordet skickas bara
// med i sjalva inloggningen.
import { supabase } from "./supabase.js";

// Ett anrop. Funktionens egna felbesked ("fel e-post eller lösenord",
// "logga in igen") lyfts fram i stallet for supabase-klientens allmanna
// "Edge Function returned a non-2xx status code".
export async function toyotaCall(body) {
  const { data, error } = await supabase.functions.invoke("drive-toyota", { body });
  if (!error) return data;
  let msg = error.message;
  let needsLogin = false;
  try {
    const j = await error.context.json();
    if (j?.error) msg = j.error;
    needsLogin = !!j?.needs_login;
  } catch { /* inget json-svar */ }
  const e = new Error(msg);
  e.needsLogin = needsLogin;
  throw e;
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// En bils synk, varv for varv tills Toyota inte har fler sidor. from/to
// (YYYY-MM-DD) anges bara for historikhamtning; utan dem tar funktionen fran
// senaste kanda resa till idag. onProgress far antalet nya resor hittills.
export async function syncToyota(deviceId, { from, to, onProgress } = {}) {
  let cursor = null;
  let total = 0;
  let busyWaits = 0;
  for (let round = 0; round < 200; round++) {
    const r = await toyotaCall({ action: "sync", device_id: deviceId, from, to, cursor });
    if (r?.busy) {
      // En annan flik eller en annan bil pa samma konto synkar just nu.
      if (++busyWaits > 20) break;
      await sleep(3000);
      continue;
    }
    total += r?.inserted ?? 0;
    onProgress?.(total);
    cursor = r?.next ?? null;
    if (!cursor) break;
  }
  return total;
}

// Alla kopplade Toyotor, de som inte synkat pa en kvart. Anropas nar appen
// oppnas och sedan med jamna mellanrum; resultatet ar antalet nya resor.
export async function syncAllToyota({ minAgeMs = 15 * 60 * 1000 } = {}) {
  const { data } = await supabase.from("drive_devices")
    .select("id, connector").eq("kind", "toyota");
  let total = 0;
  for (const d of data ?? []) {
    const c = d.connector ?? {};
    if (c.needs_login) continue;
    const last = c.last_sync ? Date.parse(c.last_sync) : 0;
    if (Date.now() - last < minAgeMs) continue;
    try {
      total += await syncToyota(d.id);
    } catch { /* felet star pa kopplingen och visas under Installningar */ }
  }
  return total;
}
