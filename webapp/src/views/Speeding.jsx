// Fortkorningen: hur stor del av den rullande tiden bilen legat over skyltad
// hastighet. Matt per resa och over tid - samma tanke som fartvarnarna
// marknadsfor, men raknad ur din egen logg.
import { useEffect, useMemo, useState } from "react";
import { supabase } from "../lib/supabase.js";
import { fmtDate, fmtDur } from "../lib/fmt.js";
import { STATUS_WARNING, STATUS_CRITICAL, SERIES } from "../lib/palette.js";
import { BarChart } from "../components/charts.jsx";

const RANGES = [
  { key: "30", label: "30 dagar", days: 30 },
  { key: "90", label: "90 dagar", days: 90 },
  { key: "all", label: "allt", days: null },
];

export default function Speeding() {
  const [trips, setTrips] = useState([]);
  const [range, setRange] = useState("90");

  useEffect(() => {
    supabase.from("drive_trips")
      .select("trip_no, start_utc, moving_s, speeding_s, max_speed_kmh")
      .order("start_utc", { ascending: true }).limit(1000)
      .then(({ data }) => setTrips(data ?? []));
  }, []);

  const filtered = useMemo(() => {
    const days = RANGES.find((r) => r.key === range)?.days;
    if (!days) return trips;
    const cut = Date.now() - days * 86400_000;
    return trips.filter((t) => t.start_utc && new Date(t.start_utc) > cut);
  }, [trips, range]);

  const agg = useMemo(() => {
    const moving = filtered.reduce((a, t) => a + (t.moving_s || 0), 0);
    const speeding = filtered.reduce((a, t) => a + (t.speeding_s || 0), 0);
    const worst = filtered.reduce(
      (a, t) => Math.max(a, t.max_speed_kmh || 0), 0,
    );
    return {
      moving, speeding,
      pct: moving > 0 ? (speeding / moving) * 100 : 0,
      worst,
    };
  }, [filtered]);

  const bars = useMemo(() =>
    filtered
      .filter((t) => (t.moving_s || 0) > 60)
      .map((t) => {
        const pct = ((t.speeding_s || 0) / t.moving_s) * 100;
        return {
          label: fmtDate(t.start_utc),
          value: pct,
          // Statusfarg efter allvar - inte seriefarg. Under 2 % ar vardagsbrus.
          color: pct >= 10 ? STATUS_CRITICAL : pct >= 2 ? STATUS_WARNING : SERIES,
          tip: `Resa ${t.trip_no} · ${pct.toFixed(1)} % över gränsen · ` +
               `${fmtDur(t.speeding_s)} av ${fmtDur(t.moving_s)}`,
        };
      }),
  [filtered]);

  return (
    <>
      <div className="card">
        <h2>Fortkörning</h2>
        <div className="filters">
          {RANGES.map((r) => (
            <button key={r.key} className={range === r.key ? "active" : ""}
              onClick={() => setRange(r.key)}>{r.label}</button>
          ))}
        </div>
        <div className="tiles">
          <div className="tile">
            <b>{agg.pct.toFixed(1)} %</b>
            <span>av rullande tid över skyltat</span>
          </div>
          <div className="tile">
            <b>{fmtDur(agg.speeding)}</b>
            <span>över gränsen totalt</span>
          </div>
          <div className="tile">
            <b>{Math.round(agg.worst)} km/h</b>
            <span>högsta uppmätta fart</span>
          </div>
        </div>
      </div>
      <div className="card">
        <h2>Andel över gränsen per resa, %</h2>
        <BarChart items={bars} unit="%" />
        <div className="legend">
          <span><span className="sw" style={{ background: SERIES }} />under 2 %</span>
          <span><span className="sw" style={{ background: STATUS_WARNING }} />2–10 %</span>
          <span><span className="sw" style={{ background: STATUS_CRITICAL }} />över 10 %</span>
        </div>
        <p className="status">
          Räknas bara när skyltad hastighet är känd – utan HASTIGHET.BIN på
          enheten är siffran noll, inte sanningen.
        </p>
      </div>
    </>
  );
}
