// Ecodrive-trenderna: poangen per resa over tid, och de harda momenten.
// Tva diagram, inte ett med tva axlar - tva matt av olika slag delar aldrig yta.
import { useEffect, useMemo, useState } from "react";
import { supabase } from "../lib/supabase.js";
import { fmtDate } from "../lib/fmt.js";
import { SERIES, STATUS_WARNING } from "../lib/palette.js";
import { LineChart, BarChart } from "../components/charts.jsx";

export default function Eco() {
  const [trips, setTrips] = useState([]);

  useEffect(() => {
    supabase.from("drive_trips")
      .select("trip_no, start_utc, eco_score, hard_events, distance_m")
      .order("start_utc", { ascending: true }).limit(1000)
      .then(({ data }) => setTrips(data ?? []));
  }, []);

  const scored = useMemo(
    () => trips.filter((t) => t.eco_score != null && (t.distance_m || 0) > 500),
    [trips],
  );

  const scoreItems = scored.map((t) => ({
    label: fmtDate(t.start_utc),
    value: t.eco_score,
    tip: `Resa ${t.trip_no} · ${Math.round(t.eco_score)} poäng`,
  }));

  const hardItems = scored.map((t) => ({
    label: fmtDate(t.start_utc),
    value: t.hard_events ?? 0,
    color: STATUS_WARNING,
    tip: `Resa ${t.trip_no} · ${t.hard_events ?? 0} hårda moment`,
  }));

  const avg = scored.length
    ? scored.reduce((a, t) => a + t.eco_score, 0) / scored.length
    : null;

  return (
    <>
      <div className="card">
        <h2>Ecodrive</h2>
        <div className="tiles">
          <div className="tile">
            <b>{avg != null ? Math.round(avg) : "–"}</b>
            <span>snittpoäng, alla resor</span>
          </div>
          <div className="tile">
            <b>{scored.length ? Math.round(scored.slice(-5).reduce((a, t) => a + t.eco_score, 0) / Math.min(5, scored.length)) : "–"}</b>
            <span>snitt senaste fem</span>
          </div>
          <div className="tile">
            <b>{scored.reduce((a, t) => a + (t.hard_events ?? 0), 0)}</b>
            <span>hårda moment totalt</span>
          </div>
        </div>
      </div>
      <div className="card">
        <h2>Ecopoäng per resa</h2>
        <LineChart items={scoreItems} color={SERIES} domainMax={100} />
      </div>
      <div className="card">
        <h2>Hårda moment per resa</h2>
        <BarChart items={hardItems} />
      </div>
    </>
  );
}
