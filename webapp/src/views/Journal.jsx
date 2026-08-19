// Korjournalen: resorna i tabell, redigerbara dar det behovs, och
// matarstallningen som raknas fram ur strackorna och stams av mot bilen.
import { useEffect, useMemo, useState } from "react";
import { supabase } from "../lib/supabase.js";
import {
  fmtKm, fmtDateTime, fmtDur, intFmt, PURPOSES, purposeLabel,
} from "../lib/fmt.js";
import { PURPOSE_COLOR } from "../lib/palette.js";

function OdometerCard({ trips }) {
  const [readings, setReadings] = useState([]);
  const [value, setValue] = useState("");
  const [status, setStatus] = useState("");

  const load = async () => {
    const { data } = await supabase
      .from("drive_odometer").select("*").order("read_at", { ascending: false })
      .limit(5);
    setReadings(data ?? []);
  };
  useEffect(() => { load(); }, []);

  // Berknad stallning: senaste avstamningen plus gps-strackan for resorna efter
  // den. Utan avstamning finns inget att rakna fran, och da sags det.
  const latest = readings[0];
  const estimated = useMemo(() => {
    if (!latest) return null;
    const after = trips.filter(
      (t) => t.end_utc && new Date(t.end_utc) > new Date(latest.read_at),
    );
    const km = after.reduce((a, t) => a + (t.distance_m || 0) / 1000, 0);
    return latest.odometer_km + km;
  }, [latest, trips]);

  const save = async () => {
    const km = parseFloat(String(value).replace(",", "."));
    if (!Number.isFinite(km) || km <= 0) {
      setStatus("skriv mätarställningen i km");
      return;
    }
    const { error } = await supabase
      .from("drive_odometer").insert({ odometer_km: km });
    if (error) { setStatus(error.message); return; }
    setValue("");
    setStatus("avstämd");
    load();
  };

  return (
    <div className="card">
      <h2>Mätarställning</h2>
      <div className="tiles">
        <div className="tile">
          <b>{estimated != null ? intFmt.format(Math.round(estimated)) : "–"}</b>
          <span>beräknad nu, km</span>
        </div>
        <div className="tile">
          <b>{latest ? intFmt.format(Math.round(latest.odometer_km)) : "–"}</b>
          <span>
            senast avstämd{latest ? ` ${fmtDateTime(latest.read_at)}` : ""}
          </span>
        </div>
        <div className="tile">
          <span>ny avstämning mot bilen</span>
          <div style={{ display: "flex", gap: ".4rem", marginTop: ".3rem" }}>
            <input type="text" inputMode="numeric" placeholder="km"
              value={value} onChange={(e) => setValue(e.target.value)}
              style={{ width: "7rem" }} />
            <button className="primary" onClick={save}>Spara</button>
          </div>
        </div>
      </div>
      <p className="status">{status}</p>
    </div>
  );
}

export default function Journal() {
  const [trips, setTrips] = useState([]);
  const [customers, setCustomers] = useState([]);
  const [status, setStatus] = useState("hämtar …");

  const load = async () => {
    const [t, c] = await Promise.all([
      supabase.from("drive_trips").select("*")
        .order("start_utc", { ascending: false }).limit(500),
      supabase.from("drive_customers").select("*").eq("active", true)
        .order("name"),
    ]);
    if (t.error) { setStatus(t.error.message); return; }
    setTrips(t.data ?? []);
    setCustomers(c.data ?? []);
    setStatus(t.data?.length ? "" : "Inga resor än – börja under Importera.");
  };
  useEffect(() => { load(); }, []);

  const patch = async (id, fields) => {
    setTrips((xs) => xs.map((t) => (t.id === id ? { ...t, ...fields } : t)));
    const { error } = await supabase
      .from("drive_trips").update(fields).eq("id", id);
    if (error) setStatus(`kunde inte spara: ${error.message}`);
  };

  const totals = useMemo(() => {
    const km = trips.reduce((a, t) => a + (t.distance_m || 0), 0) / 1000;
    const per = { privat: 0, foretag: 0, diffust: 0, omarkt: 0 };
    for (const t of trips) per[t.purpose ?? "omarkt"] += (t.distance_m || 0) / 1000;
    const moving = trips.reduce((a, t) => a + (t.moving_s || 0), 0);
    // Osignerade resor ar hal i bevisningen om journalen nagonsin granskas -
    // de raknas har sa att de blir atgardade i tid, inte upptackta i efterhand.
    const unsigned = trips.filter(
      (t) => !t.purpose || t.purpose === "omarkt" || t.purpose === "diffust",
    ).length;
    return { km, per, moving, unsigned };
  }, [trips]);

  const exportCsv = () => {
    const rows = [[
      "resa", "start", "mal", "km", "syfte", "kund", "maxfart_kmh",
      "fortkorning_min", "ecopoang", "matarstallning_km", "anteckning",
    ].join(";")];
    for (const t of [...trips].reverse()) {
      rows.push([
        t.trip_no, fmtDateTime(t.start_utc), fmtDateTime(t.end_utc),
        fmtKm(t.distance_m), purposeLabel(t.purpose), t.customer ?? "",
        t.max_speed_kmh ? Math.round(t.max_speed_kmh) : "",
        Math.round((t.speeding_s || 0) / 60),
        t.eco_score != null ? Math.round(t.eco_score) : "",
        t.odometer_km ?? "", (t.notes ?? "").replaceAll(";", ","),
      ].join(";"));
    }
    // ﻿ far Excel att lasa filen som utf-8 i stallet for att gissa.
    const blob = new Blob(["﻿" + rows.join("\n")], {
      type: "text/csv;charset=utf-8",
    });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "korjournal.csv";
    a.click();
    URL.revokeObjectURL(a.href);
  };

  return (
    <>
      <div className="card">
        <h2>Överblick</h2>
        <div className="tiles">
          <div className="tile"><b>{intFmt.format(Math.round(totals.km))}</b><span>körda km</span></div>
          <div className="tile"><b>{trips.length}</b><span>resor</span></div>
          <div className="tile"><b>{fmtDur(totals.moving)}</b><span>rullande tid</span></div>
          <div className="tile">
            <b>{totals.km > 0 ? Math.round((totals.per.foretag / totals.km) * 100) : 0} %</b>
            <span>företag av körda km</span>
          </div>
          <div className="tile">
            <b style={{ color: totals.unsigned ? "var(--warn)" : "var(--green)" }}>
              {totals.unsigned}
            </b>
            <span>osignerade resor</span>
          </div>
        </div>
      </div>

      <OdometerCard trips={trips} />

      <div className="card">
        <h2>Resor</h2>
        <p className="status">{status}</p>
        <div style={{ overflowX: "auto" }}>
          <table className="journal">
            <thead>
              <tr>
                <th></th><th>Nr</th><th>Start</th><th>Mål</th><th>Km</th>
                <th>Syfte</th><th>Kund</th><th>Mätare</th><th>Anteckning</th>
              </tr>
            </thead>
            <tbody>
              {trips.map((t) => (
                <tr key={t.id}>
                  <td><span className="chip"
                    style={{ background: PURPOSE_COLOR[t.purpose] ?? PURPOSE_COLOR.omarkt }} /></td>
                  <td>{t.trip_no}</td>
                  <td>{fmtDateTime(t.start_utc)}</td>
                  <td>{fmtDateTime(t.end_utc)}</td>
                  <td>{fmtKm(t.distance_m)}</td>
                  <td>
                    <select value={t.purpose ?? "omarkt"}
                      onChange={(e) => patch(t.id, { purpose: e.target.value })}>
                      {t.purpose === "omarkt" && <option value="omarkt">Omärkt</option>}
                      {PURPOSES.map((p) => (
                        <option key={p.value} value={p.value}>{p.label}</option>
                      ))}
                    </select>
                  </td>
                  <td>
                    <select value={t.customer ?? ""}
                      onChange={(e) => patch(t.id, { customer: e.target.value || null })}>
                      <option value="">–</option>
                      {customers.map((c) => (
                        <option key={c.id} value={c.name}>{c.name}</option>
                      ))}
                      {t.customer && !customers.some((c) => c.name === t.customer) && (
                        <option value={t.customer}>{t.customer}</option>
                      )}
                    </select>
                  </td>
                  <td>
                    <input type="text" inputMode="numeric" placeholder="km"
                      style={{ width: "5.5rem" }}
                      defaultValue={t.odometer_km ?? ""}
                      onBlur={(e) => {
                        const v = parseFloat(e.target.value.replace(",", "."));
                        patch(t.id, { odometer_km: Number.isFinite(v) ? v : null });
                      }} />
                  </td>
                  <td>
                    <input type="text" style={{ width: "10rem" }}
                      defaultValue={t.notes ?? ""}
                      onBlur={(e) => patch(t.id, { notes: e.target.value || null })} />
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <p style={{ marginTop: ".8rem" }}>
          <button className="ghost" onClick={exportCsv}>Exportera körjournal (CSV)</button>
        </p>
      </div>
    </>
  );
}
