// AI-analysen av kormonster. Anropen gar via var egen Vercel-funktion, som
// lagger pa Anthropic-nyckeln ur sina miljovariabler - nyckeln ligger inte i
// nagon webblasare och lamnar aldrig servern. Proxyn slapper bara fram
// inloggade, sa sessionens token foljer med varje anrop.
import { useEffect, useState } from "react";
import Anthropic from "@anthropic-ai/sdk";
import { supabase } from "../lib/supabase.js";
import { fmtDur, intFmt } from "../lib/fmt.js";

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

// Ett minikort per sparad analys: datum, underlag och en kort forsmak.
// Klicket faller ut hela texten; senaste ligger overst i listan.
function AnalysisCard({ row, onRemove }) {
  const [open, setOpen] = useState(false);
  const when = new Date(row.at).toLocaleString("sv-SE", {
    dateStyle: "medium", timeStyle: "short",
  });
  const preview = row.content.length > 220
    ? row.content.slice(0, 220).trimEnd() + " …"
    : row.content;
  return (
    <div className="ai-mini" onClick={() => setOpen((o) => !o)}>
      <div className="ai-mini-head">
        <b>{when}</b>
        <span>
          {row.trips != null ? `${intFmt.format(row.trips)} resor` : ""}
          {row.km != null ? ` · ${intFmt.format(row.km)} km` : ""}
        </span>
      </div>
      <div className="ai-out" style={open ? undefined : { minHeight: 0 }}>
        {open ? row.content : preview}
      </div>
      {open && (
        <p style={{ margin: ".6rem 0 0" }}>
          <button className="ghost" onClick={(e) => {
            e.stopPropagation();
            if (confirm("Ta bort den här analysen?")) onRemove(row.id);
          }}>ta bort</button>
        </p>
      )}
    </div>
  );
}

export default function Ai() {
  const [trips, setTrips] = useState([]);
  const [out, setOut] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [history, setHistory] = useState([]);

  useEffect(() => {
    supabase.from("drive_trips").select("*")
      .order("start_utc", { ascending: false }).limit(400)
      .then(({ data }) => setTrips(data ?? []));
    supabase.from("drive_analyses").select("*")
      .order("at", { ascending: false }).limit(50)
      .then(({ data }) => setHistory(data ?? []));
  }, []);

  const removeAnalysis = async (id) => {
    await supabase.from("drive_analyses").delete().eq("id", id);
    setHistory((h) => h.filter((r) => r.id !== id));
  };

  const analyze = async () => {
    if (!trips.length) { setError("Inga resor att analysera än."); return; }

    setBusy(true); setError(""); setOut("");
    try {
      // SDK:t pekas mot var proxy; x-api-key-huvudet det skickar ar en
      // platshallare som proxyn byter ut mot den riktiga nyckeln.
      const { data: { session } } = await supabase.auth.getSession();
      const client = new Anthropic({
        apiKey: "hanteras-av-vercel",
        baseURL: `${window.location.origin}/api/anthropic`,
        dangerouslyAllowBrowser: true,
        defaultHeaders: { Authorization: `Bearer ${session?.access_token ?? ""}` },
      });

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
      } else {
        // Fardig analys sparas som ett minikort i historiken; den
        // strommade rutan toms - kortet overst AR den nya analysen.
        const text = final.content
          .filter((b) => b.type === "text").map((b) => b.text).join("");
        const km = Math.round(
          trips.reduce((a, t) => a + (t.distance_m || 0), 0) / 1000);
        const { data, error: dbErr } = await supabase
          .from("drive_analyses")
          .insert({ content: text, trips: trips.length, km })
          .select().single();
        if (dbErr) {
          setError("Analysen kunde inte sparas: " + dbErr.message);
        } else {
          setHistory((h) => [data, ...h]);
          setOut("");
        }
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
        Nyckeln ligger i Vercels miljövariabler (ANTHROPIC_API_KEY), inte i
        webbläsaren. Underlaget är {intFmt.format(trips.length)} resor,{" "}
        {fmtDur(trips.reduce((a, t) => a + (t.moving_s || 0), 0))} rullande tid.
      </p>
      <p>
        <button className="primary" onClick={analyze} disabled={busy}>
          {busy ? "analyserar …" : "Analysera körmönster"}
        </button>
      </p>
      {error && <p className="status error">{error}</p>}
      {out && <div className="ai-out">{out}</div>}
      {history.length > 0 && (
        <>
          <h3 style={{ margin: "1.2rem 0 .5rem" }}>Tidigare analyser</h3>
          {history.map((row) => (
            <AnalysisCard key={row.id} row={row} onRemove={removeAnalysis} />
          ))}
        </>
      )}
    </div>
  );
}
