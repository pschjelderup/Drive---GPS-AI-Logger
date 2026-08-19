// Arsrapporten: det underlag Skatteverket fragar efter om korjournalen nagonsin
// granskas - matarstallning vid arets borjan och slut, korda km per syfte,
// och varje tjansteresa med datum, strackning, kund och arende. Sidan ar
// utskriftsvanlig (Skriv ut ger ren tabell utan skal) och gar att ta ut som CSV.
//
// Vid en granskning ar bevisbordan omvand: det ar journalen som ska visa att
// privatkorningen var det den pastods vara. Darfor raknas osignerade resor
// (omarkt/diffust) upp har - varje sadan ar ett hal i bevisningen.
import { useEffect, useMemo, useState } from "react";
import { supabase } from "../lib/supabase.js";
import { fmtKm, fmtDateTime, intFmt, purposeLabel } from "../lib/fmt.js";

const RATE_KEY = "drivelogger_milersattning";

export default function Report() {
  const [trips, setTrips] = useState([]);
  const [readings, setReadings] = useState([]);
  const [year, setYear] = useState(new Date().getFullYear());
  const [rate, setRate] = useState(
    () => localStorage.getItem(RATE_KEY) ?? "25",
  );
  const [status, setStatus] = useState("hämtar …");

  useEffect(() => {
    (async () => {
      const [t, o] = await Promise.all([
        supabase.from("drive_trips").select("*")
          .order("start_utc", { ascending: true }).limit(2000),
        supabase.from("drive_odometer").select("*")
          .order("read_at", { ascending: true }),
      ]);
      if (t.error) { setStatus(t.error.message); return; }
      setTrips(t.data ?? []);
      setReadings(o.data ?? []);
      setStatus("");
    })();
  }, []);

  const years = useMemo(() => {
    const ys = new Set(
      trips.filter((t) => t.start_utc)
        .map((t) => new Date(t.start_utc).getFullYear()),
    );
    ys.add(new Date().getFullYear());
    return [...ys].sort((a, b) => b - a);
  }, [trips]);

  const data = useMemo(() => {
    const inYear = trips.filter(
      (t) => t.start_utc && new Date(t.start_utc).getFullYear() === year,
    );
    const per = { privat: 0, foretag: 0, diffust: 0, omarkt: 0 };
    for (const t of inYear) per[t.purpose ?? "omarkt"] += (t.distance_m || 0) / 1000;
    const km = per.privat + per.foretag + per.diffust + per.omarkt;
    const unsigned = inYear.filter(
      (t) => t.purpose === "omarkt" || t.purpose === "diffust",
    ).length;

    // Matarstallning: sista avlasningen fore arets borjan respektive arets
    // slut. Saknas avlasningar sags det - en rapport ska inte hitta pa.
    const start = new Date(`${year}-01-01T00:00:00Z`);
    const end = new Date(`${year + 1}-01-01T00:00:00Z`);
    const before = readings.filter((r) => new Date(r.read_at) < start).at(-1);
    const last = readings.filter((r) => new Date(r.read_at) < end).at(-1);

    return { inYear, per, km, unsigned, odoStart: before, odoEnd: last };
  }, [trips, readings, year]);

  const rateNum = parseFloat(String(rate).replace(",", ".")) || 0;
  const ersattning = (data.per.foretag / 10) * rateNum;

  const saveRate = (v) => {
    setRate(v);
    localStorage.setItem(RATE_KEY, v);
  };

  const exportCsv = () => {
    const rows = [[
      "resa", "start", "mal", "km", "syfte", "kund", "arende",
      "matarstallning_km",
    ].join(";")];
    for (const t of data.inYear) {
      rows.push([
        t.trip_no, fmtDateTime(t.start_utc), fmtDateTime(t.end_utc),
        fmtKm(t.distance_m), purposeLabel(t.purpose), t.customer ?? "",
        (t.notes ?? "").replaceAll(";", ","), t.odometer_km ?? "",
      ].join(";"));
    }
    const blob = new Blob(["﻿" + rows.join("\n")], {
      type: "text/csv;charset=utf-8",
    });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = `korjournal-${year}.csv`;
    a.click();
    URL.revokeObjectURL(a.href);
  };

  return (
    <>
      <div className="card noprint">
        <h2>Årsrapport</h2>
        <div style={{ display: "flex", gap: ".6rem", flexWrap: "wrap", alignItems: "center" }}>
          <select value={year} onChange={(e) => setYear(Number(e.target.value))}>
            {years.map((y) => <option key={y} value={y}>{y}</option>)}
          </select>
          <label style={{ display: "flex", gap: ".4rem", alignItems: "center", fontSize: ".88rem", color: "var(--dim)" }}>
            ersättning kr/mil
            <input type="text" inputMode="decimal" value={rate}
              style={{ width: "4.5rem" }}
              onChange={(e) => saveRate(e.target.value)} />
          </label>
          <button className="ghost" onClick={() => window.print()}>Skriv ut</button>
          <button className="ghost" onClick={exportCsv}>CSV</button>
        </div>
        <p className="status" style={{ marginBottom: 0 }}>
          Schablon 2026: 25 kr/mil egen bil · 12 kr/mil förmånsbil (9,50 el).
          Beloppet räknas på företagskörningen.
        </p>
      </div>

      <div className="card">
        <h2>Körjournal {year} – sammanställning</h2>
        <p className="status">{status}</p>
        <div className="tiles">
          <div className="tile"><b>{intFmt.format(Math.round(data.km))}</b><span>körda km</span></div>
          <div className="tile"><b>{intFmt.format(Math.round(data.per.foretag))}</b><span>företag, km</span></div>
          <div className="tile"><b>{intFmt.format(Math.round(data.per.privat))}</b><span>privat, km</span></div>
          <div className="tile"><b>{data.inYear.length}</b><span>resor</span></div>
          <div className="tile">
            <b>{data.odoStart ? intFmt.format(Math.round(data.odoStart.odometer_km)) : "–"}</b>
            <span>mätarställning vid årets början</span>
          </div>
          <div className="tile">
            <b>{data.odoEnd ? intFmt.format(Math.round(data.odoEnd.odometer_km)) : "–"}</b>
            <span>senast avstämd under året</span>
          </div>
          <div className="tile">
            <b>{intFmt.format(Math.round(ersattning))} kr</b>
            <span>milersättning ({rateNum} kr/mil × företagsmil)</span>
          </div>
          <div className="tile">
            <b style={{ color: data.unsigned ? "var(--warn)" : "var(--green)" }}>
              {data.unsigned}
            </b>
            <span>osignerade resor (omärkt/diffust)</span>
          </div>
        </div>
        {!data.odoStart && (
          <p className="status" style={{ marginTop: ".6rem" }}>
            Ingen mätaravstämning före {year} – Skatteverket vill se ställningen
            vid årets början och slut. Stäm av under Körjournal.
          </p>
        )}
      </div>

      <div className="card">
        <h2>Resor {year}</h2>
        <div style={{ overflowX: "auto" }}>
          <table className="journal">
            <thead>
              <tr>
                <th>Nr</th><th>Start</th><th>Mål</th><th>Km</th>
                <th>Syfte</th><th>Kund</th><th>Ärende</th><th>Mätare</th>
              </tr>
            </thead>
            <tbody>
              {data.inYear.map((t) => (
                <tr key={t.id}>
                  <td>{t.trip_no}</td>
                  <td>{fmtDateTime(t.start_utc)}</td>
                  <td>{fmtDateTime(t.end_utc)}</td>
                  <td>{fmtKm(t.distance_m)}</td>
                  <td>{purposeLabel(t.purpose)}</td>
                  <td>{t.customer ?? ""}</td>
                  <td>{t.notes ?? ""}</td>
                  <td>{t.odometer_km ?? ""}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </div>
    </>
  );
}
