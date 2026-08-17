// AI-analysen av kormonster. Din egen Anthropic-nyckel, inklistrad under
// Installningar, lagrad i webblasarens localStorage - den hamnar aldrig i
// databasen och lamnar aldrig klienten annat an i anropen till Anthropic.
import { useEffect, useMemo, useState } from "react";
import Anthropic from "@anthropic-ai/sdk";
import { supabase } from "../lib/supabase.js";
import { fmtDur, intFmt } from "../lib/fmt.js";

export const AI_KEY_STORAGE = "drivelogger_anthropic_key";

// Underlaget: sammandrag plus de senaste resorna, kompakt nog att aldrig bli
// dyrt, rikt nog att sага nagot. Ravardena ur databasen, inga omdomen - de ar
// modellens jobb.
function buildContext(trips) {
  const total = trips.reduce((a, t) => a + (t.distance_m || 0), 0) / 1000;
  const moving = trips.reduce((a, t) => a + (t.moving_s || 0), 0);
  const speeding = trips.reduce((a, t) => a + (t.speeding_s || 0), 0);
  const per = {};
  for (const t of trips) {
    per[t.purpose ?? "omarkt"] = (per[t.purpose ?? "omarkt"] || 0) + (t.distance_m || 0) / 1000;
  }
  const recent = trips.slice(0, 40).map((t) => ({
    nr: t.trip_no,
    start: t.start_utc,
    km: Math.round((t.distance_m || 0) / 100) / 10,
    min: Math.round((t.moving_s || 0) / 60),
    syfte: t.purpose,
    kund: t.customer || undefined,
    maxfart: t.max_speed_kmh ? Math.round(t.max_speed_kmh) : undefined,
    fortkorning_min: Math.round((t.speeding_s || 0) / 60),
    ecopoang: t.eco_score != null ? Math.round(t.eco_score) : undefined,
    harda_moment: t.hard_events ?? undefined,
    start_pos: t.start_lat ? [t.start_lat.toFixed(3), t.start_lon.toFixed(3)] : undefined,
    mal_pos: t.end_lat ? [t.end_lat.toFixed(3), t.end_lon.toFixed(3)] : undefined,
  }));

  return JSON.stringify({
    sammandrag: {
      antal_resor: trips.length,
      totalt_km: Math.round(total),
      rullande_tid_min: Math.round(moving / 60),
      fortkorning_min: Math.round(speeding / 60),
      km_per_syfte: Object.fromEntries(
        Object.entries(per).map(([k, v]) => [k, Math.round(v)]),
      ),
    },
    senaste_resorna: recent,
  });
}

const SYSTEM = `Du är en körmönsteranalytiker för en företagsbilsförare i Sverige.
Du får förarens egen körjournal som JSON: ett sammandrag och de senaste resorna,
med sträckor, tider, syften (privat/företag/diffust), kunder, toppfarter,
fortkörningsminuter, ecodrive-poäng (0-100, högre är mjukare körning) och
avrundade start-/målpositioner.

Analysera på svenska. Var konkret och siffersatt - peka på mönster i datan, inte
plattityder. Strukturera i korta avsnitt: körmönster och rutiner, fördelning
privat/företag och vad den betyder för körjournalen, fart och fortkörning,
ecodrive-utveckling, och till sist två eller tre konkreta observationer värda att
agera på. Om datan är tunn för någon slutsats, säg det hellre än att gissa.`;

export default function Ai() {
  const [trips, setTrips] = useState([]);
  const [out, setOut] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const hasKey = useMemo(() => !!localStorage.getItem(AI_KEY_STORAGE), [busy]);

  useEffect(() => {
    supabase.from("drive_trips").select("*")
      .order("start_utc", { ascending: false }).limit(400)
      .then(({ data }) => setTrips(data ?? []));
  }, []);

  const analyze = async () => {
    const apiKey = localStorage.getItem(AI_KEY_STORAGE);
    if (!apiKey) { setError("Lägg in din Anthropic-nyckel under Inställningar först."); return; }
    if (!trips.length) { setError("Inga resor att analysera än."); return; }

    setBusy(true); setError(""); setOut("");
    try {
      // Nyckeln ar anvandarens egen, sa direktanrop fran webblasaren ar just
      // det som ar meningen - darav flaggan med det avskrackande namnet.
      const client = new Anthropic({ apiKey, dangerouslyAllowBrowser: true });

      // Reservvag pa serversidan: skulle en forfragan nekas av sakerhetsskal
      // dirigeras den om till en annan modell i stallet for att bli ett fel.
      const stream = client.beta.messages.stream({
        model: "claude-opus-5",
        max_tokens: 8000,
        betas: ["server-side-fallback-2026-07-01"],
        fallbacks: "default",
        system: SYSTEM,
        messages: [{ role: "user", content: buildContext(trips) }],
      });

      stream.on("text", (t) => setOut((s) => s + t));
      const final = await stream.finalMessage();
      if (final.stop_reason === "refusal") {
        setError("Analysen avböjdes av modellen. Försök igen.");
      }
    } catch (e) {
      setError(e?.message ?? String(e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="card">
      <h2>AI-analys av körmönster</h2>
      <p style={{ color: "var(--dim)", marginTop: 0 }}>
        Analysen körs med din egen Anthropic-nyckel, direkt från webbläsaren.
        Underlaget är {intFmt.format(trips.length)} resor,{" "}
        {fmtDur(trips.reduce((a, t) => a + (t.moving_s || 0), 0))} rullande tid.
      </p>
      <p>
        <button className="primary" onClick={analyze} disabled={busy || !hasKey}>
          {busy ? "analyserar …" : "Analysera körmönster"}
        </button>
        {!hasKey && (
          <span className="status" style={{ marginLeft: ".8rem" }}>
            nyckel saknas – se Inställningar
          </span>
        )}
      </p>
      {error && <p className="status error">{error}</p>}
      {out && <div className="ai-out">{out}</div>}
    </div>
  );
}
