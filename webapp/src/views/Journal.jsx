// Korjournalen: resorna i tabell, redigerbara dar det behovs, och
// matarstallningen som raknas fram ur strackorna och stams av mot bilen.
import { useEffect, useMemo, useRef, useState } from "react";
import { supabase, GPX_BUCKET } from "../lib/supabase.js";
import {
  fmtKm, fmtDateTime, fmtDur, intFmt, PURPOSES, purposeLabel,
} from "../lib/fmt.js";
import { PURPOSE_COLOR } from "../lib/palette.js";
import { parseGpx } from "../lib/gpx.js";
import { vehicleLabel, tripVehicleId } from "../lib/vehicles.js";

// Sparet i miniformat, pa riktig kartbotten: OSM-plattorna som tacker sparets
// rektangel raknas fram och laggs som bilder, sparet ritas ovanpa med vit
// kant sa det lyfter ur kartbilden. Ingen kartmotor per kort - bara bilder
// och en SVG, sa hundra kort kostar ingenting.
function TrackMini({ pts, color }) {
  const boxRef = useRef(null);
  const [dim, setDim] = useState(null);
  useEffect(() => {
    const el = boxRef.current;
    if (el) setDim({ w: el.clientWidth || 260, h: el.clientHeight || 96 });
  }, []);

  let content = null;
  if (pts === undefined) {
    content = <span className="mm-note">hämtar spår …</span>;
  } else if (!pts || pts.length < 2) {
    content = <span className="mm-note">inget spår</span>;
  } else if (dim) {
    const { w, h } = dim;
    const merc = (lat, lon) => {
      const x = (lon + 180) / 360;
      const sn = Math.sin((lat * Math.PI) / 180);
      const y = 0.5 - Math.log((1 + sn) / (1 - sn)) / (4 * Math.PI);
      return [x, y];
    };
    let minX = 1, maxX = 0, minY = 1, maxY = 0;
    for (const [la, lo] of pts) {
      const [x, y] = merc(la, lo);
      if (x < minX) minX = x;
      if (x > maxX) maxX = x;
      if (y < minY) minY = y;
      if (y > maxY) maxY = y;
    }
    const dx = Math.max(maxX - minX, 1e-9);
    const dy = Math.max(maxY - minY, 1e-9);
    // Storsta zoom dar sparet ryms med lite luft.
    const z = Math.max(3, Math.min(17, Math.floor(Math.min(
      Math.log2((w * 0.8) / (256 * dx)),
      Math.log2((h * 0.8) / (256 * dy)),
    ))));
    const world = 256 * 2 ** z;
    const left = ((minX + maxX) / 2) * world - w / 2;
    const top = ((minY + maxY) / 2) * world - h / 2;

    const tiles = [];
    const maxTile = 2 ** z;
    for (let tx = Math.floor(left / 256); tx * 256 < left + w; tx++) {
      for (let ty = Math.floor(top / 256); ty * 256 < top + h; ty++) {
        if (ty < 0 || ty >= maxTile) continue;
        const wx = ((tx % maxTile) + maxTile) % maxTile;
        tiles.push(
          <img key={`${tx}:${ty}`} alt=""
            src={`https://tile.openstreetmap.org/${z}/${wx}/${ty}.png`}
            style={{ left: tx * 256 - left, top: ty * 256 - top }} />,
        );
      }
    }

    const step = Math.max(1, Math.floor(pts.length / 300));
    const d = [];
    const px = (la, lo) => {
      const [x, y] = merc(la, lo);
      return `${(x * world - left).toFixed(1)},${(y * world - top).toFixed(1)}`;
    };
    for (let i = 0; i < pts.length; i += step) {
      d.push(`${i ? "L" : "M"}${px(pts[i][0], pts[i][1])}`);
    }
    const last = pts[pts.length - 1];
    d.push(`L${px(last[0], last[1])}`);
    const [sx, sy] = px(pts[0][0], pts[0][1]).split(",");
    const [ex, ey] = px(last[0], last[1]).split(",");

    content = (
      <>
        {tiles}
        <svg viewBox={`0 0 ${w} ${h}`} aria-hidden="true">
          <path d={d.join(" ")} fill="none" stroke="#ffffff" strokeWidth="5"
            strokeLinejoin="round" strokeLinecap="round" opacity="0.9" />
          <path d={d.join(" ")} fill="none" style={{ stroke: color }}
            strokeWidth="2.5" strokeLinejoin="round" strokeLinecap="round" />
          <circle cx={sx} cy={sy} r="3.5" fill="#ffffff"
            style={{ stroke: color }} strokeWidth="2" />
          <circle cx={ex} cy={ey} r="4" style={{ fill: color }}
            stroke="#ffffff" strokeWidth="1.5" />
        </svg>
        <span className="osm">© OpenStreetMap</span>
      </>
    );
  }

  return <div ref={boxRef} className="minimap">{content}</div>;
}

// Resorna som minikort: sparet, strackan, tiden och det vasentliga. Sex
// visas fran borjan och "Ladda fler" tar resten i klumpar - sparen hamtas
// bara for de kort som faktiskt visas. Ett klick pa kortet oppnar resekortet
// med allt som finns om resan.
function MiniCards({ trips, onOpen }) {
  const [tracks, setTracks] = useState({});
  const ids = trips.map((t) => t.id).join(",");

  useEffect(() => {
    let alive = true;
    (async () => {
      for (const t of trips) {
        if (!t.gpx_path) {
          setTracks((m) => (t.id in m ? m : { ...m, [t.id]: null }));
          continue;
        }
        if (t.id in tracks) continue;
        const { data } = await supabase.storage
          .from(GPX_BUCKET).download(t.gpx_path);
        const pts = data ? parseGpx(await data.text()) : null;
        if (!alive) return;
        setTracks((m) => ({ ...m, [t.id]: pts }));
      }
    })();
    return () => { alive = false; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [ids]);

  if (!trips.length) return null;

  return (
    <div className="minicards">
      {trips.map((t) => {
        const color = PURPOSE_COLOR[t.purpose] ?? PURPOSE_COLOR.omarkt;
        return (
          <div className="minicard" key={t.id} role="button" tabIndex={0}
            onClick={() => onOpen?.(t)}
            onKeyDown={(e) => e.key === "Enter" && onOpen?.(t)}>
            <div className="mc-accent" style={{ background: color }} />
            <div className="mc-head">
              <b>Resa {t.trip_no}</b>
              <span>{fmtDateTime(t.start_utc)}</span>
            </div>
            <TrackMini pts={tracks[t.id]} color={color} />
            <div className="mc-stats">
              <span><b>{fmtKm(t.distance_m)}</b> km</span>
              <span><b>{t.moving_s != null ? fmtDur(t.moving_s) : "–"}</b></span>
              {t.max_speed_kmh ? (
                <span><b>{Math.round(t.max_speed_kmh)}</b> km/h max</span>
              ) : null}
              {t.eco_score != null ? (
                <span><b>{Math.round(t.eco_score)}</b> eco</span>
              ) : null}
            </div>
            <div className="mc-foot">
              {purposeLabel(t.purpose)}
              {t.customer ? ` · ${t.customer}` : ""}
              {t.speeding_s ? ` · ${Math.round(t.speeding_s / 60)} min över gränsen` : ""}
            </div>
            {(t.start_place || t.end_place) && (
              <div className="mc-foot">
                {t.start_place ?? "…"} → {t.end_place ?? "…"}
              </div>
            )}
          </div>
        );
      })}
    </div>
  );
}

// Platsvaljaren: klicket slar upp Google-platser inom 600 meter fran
// koordinaten, narmast forst - och den som soker fritext far traffar med
// dragning mot positionen i stallet. Valet sparas pa resan. Uppslag sker
// forst vid klick: femhundra resor ganger tva platser vore annars en dyr sida.
function PlacePicker({ lat, lon, value, onPick }) {
  const [open, setOpen] = useState(false);
  const [busy, setBusy] = useState(false);
  const [q, setQ] = useState("");
  const [options, setOptions] = useState([]);
  const [err, setErr] = useState("");

  const usable = Number.isFinite(lat) && Number.isFinite(lon) && (lat || lon);
  if (!usable) return value ? <span className="place">{value}</span> : null;

  const search = async (text) => {
    setBusy(true); setErr("");
    try {
      const { data: { session } } = await supabase.auth.getSession();
      const url = `/api/places?lat=${lat}&lon=${lon}` +
        (text ? `&q=${encodeURIComponent(text)}` : "");
      const res = await fetch(url, {
        headers: { Authorization: `Bearer ${session?.access_token ?? ""}` },
      });
      const body = await res.json();
      if (!res.ok) throw new Error(body.error ?? `svar ${res.status}`);
      setOptions(body.places ?? []);
    } catch (e) {
      setErr(e.message);
      setOptions([]);
    } finally {
      setBusy(false);
    }
  };

  if (!open) {
    return (
      <span className="place">
        {value ? `${value} ` : ""}
        <button className="ghost mini"
          onClick={() => { setOpen(true); setQ(""); search(""); }}>
          {value ? "ändra" : "plats …"}
        </button>
      </span>
    );
  }

  return (
    <div className="placepop">
      <input autoFocus type="text" placeholder="sök plats – tomt visar närmaste"
        value={q}
        onChange={(e) => setQ(e.target.value)}
        onKeyDown={(e) => {
          if (e.key === "Enter") search(q.trim());
          if (e.key === "Escape") setOpen(false);
        }} />
      <div className="placelist">
        {busy && <span className="status">hämtar …</span>}
        {err && <span className="status error">{err}</span>}
        {!busy && !err && !options.length && (
          <span className="status">inget hittat – sök eller stäng</span>
        )}
        {!busy && options.map((p, i) => (
          <button key={i} type="button"
            onClick={() => { onPick(p.name); setOpen(false); }}>
            {p.name}{p.distance_m >= 0 ? ` · ${p.distance_m} m` : ""}
          </button>
        ))}
        {value && (
          <button type="button" className="danger"
            onClick={() => { onPick(null); setOpen(false); }}>
            rensa platsen
          </button>
        )}
        <button type="button" onClick={() => setOpen(false)}>stäng</button>
      </div>
    </div>
  );
}

// Resekortet: hela resan pa ett stalle. Det som ar matt visas; det som ar
// manniskans (syfte, kund, bil, platser, matarstallning, arende) andras har
// och sparas direkt. Koordinaterna lankar till Google Maps.
function TripModal({ trip, customers, vehicles, patch, onClose }) {
  const [track, setTrack] = useState(undefined);
  const gpxPath = trip?.gpx_path;
  useEffect(() => {
    let alive = true;
    setTrack(undefined);
    if (!gpxPath) { setTrack(null); return; }
    supabase.storage.from(GPX_BUCKET).download(gpxPath).then(async ({ data }) => {
      if (alive) setTrack(data ? parseGpx(await data.text()) : null);
    });
    return () => { alive = false; };
  }, [gpxPath]);

  if (!trip) return null;
  const t = trip;
  const color = PURPOSE_COLOR[t.purpose] ?? PURPOSE_COLOR.omarkt;
  const avgKmh = t.moving_s > 0 ? (t.distance_m / t.moving_s) * 3.6 : null;
  const maps = (lat, lon) =>
    `https://www.google.com/maps?q=${lat},${lon}`;

  const kv = (label, value) => (
    <div className="kv"><span>{label}</span><b>{value}</b></div>
  );

  return (
    <div className="modal-scrim" onClick={onClose}>
      <div className="modal card" onClick={(e) => e.stopPropagation()}>
        <div className="mc-head" style={{ marginBottom: ".5rem" }}>
          <b style={{ fontSize: "1.1rem" }}>Resa {t.trip_no}</b>
          <button className="ghost" onClick={onClose}>stäng</button>
        </div>
        <TrackMini pts={track} color={color} />

        <div className="kvgrid">
          {kv("Start", <>
            {fmtDateTime(t.start_utc)}
            {t.start_lat ? <>{" "}
              <a href={maps(t.start_lat, t.start_lon)} target="_blank"
                rel="noreferrer">karta</a></> : null}
          </>)}
          {kv("Mål", <>
            {fmtDateTime(t.end_utc)}
            {t.end_lat ? <>{" "}
              <a href={maps(t.end_lat, t.end_lon)} target="_blank"
                rel="noreferrer">karta</a></> : null}
          </>)}
          {kv("Sträcka", `${fmtKm(t.distance_m)} km`)}
          {kv("Rullande tid", t.moving_s != null ? fmtDur(t.moving_s) : "–")}
          {kv("Medelfart", avgKmh != null ? `${Math.round(avgKmh)} km/h` : "–")}
          {kv("Toppfart", t.max_speed_kmh ? `${Math.round(t.max_speed_kmh)} km/h` : "–")}
          {kv("Över gränsen", t.speeding_s != null ? fmtDur(t.speeding_s) : "–")}
          {kv("Ecopoäng", t.eco_score != null ? Math.round(t.eco_score) : "omätt")}
          {kv("Hårda moment", t.hard_events ?? "–")}
          {kv("Spårpunkter", t.points ?? "–")}
          {kv("Avslut", t.end_reason ?? "–")}
          {kv("GPX", t.gpx_name ?? "–")}
        </div>

        <div className="kvgrid" style={{ marginTop: ".6rem" }}>
          <div className="kv"><span>Syfte</span>
            <select value={t.purpose ?? "omarkt"}
              onChange={(e) => patch(t.id, { purpose: e.target.value })}>
              {t.purpose === "omarkt" && <option value="omarkt">Omärkt</option>}
              {PURPOSES.map((p) => (
                <option key={p.value} value={p.value}>{p.label}</option>
              ))}
            </select>
          </div>
          <div className="kv"><span>Kund</span>
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
          </div>
          {vehicles.length > 1 && (
            <div className="kv"><span>Bil</span>
              <select value={tripVehicleId(t, vehicles) ?? ""}
                onChange={(e) => patch(t.id, {
                  vehicle_id: e.target.value ? Number(e.target.value) : null,
                })}>
                {vehicles.map((v) => (
                  <option key={v.id} value={v.id}>{vehicleLabel(v)}</option>
                ))}
              </select>
            </div>
          )}
          <div className="kv"><span>Startplats</span>
            <PlacePicker lat={t.start_lat} lon={t.start_lon}
              value={t.start_place}
              onPick={(name) => patch(t.id, { start_place: name })} />
          </div>
          <div className="kv"><span>Målplats</span>
            <PlacePicker lat={t.end_lat} lon={t.end_lon}
              value={t.end_place}
              onPick={(name) => patch(t.id, { end_place: name })} />
          </div>
          <div className="kv"><span>Mätarställning</span>
            <input type="text" inputMode="numeric" placeholder="km"
              style={{ width: "7rem" }}
              defaultValue={t.odometer_km ?? ""}
              onBlur={(e) => {
                const v = parseFloat(e.target.value.replace(",", "."));
                patch(t.id, { odometer_km: Number.isFinite(v) ? v : null });
              }} />
          </div>
        </div>

        <div className="kv" style={{ marginTop: ".6rem" }}>
          <span>Ärende</span>
          <input type="text" style={{ width: "100%" }}
            defaultValue={t.notes ?? ""}
            onBlur={(e) => patch(t.id, { notes: e.target.value || null })} />
        </div>
      </div>
    </div>
  );
}

function OdometerCard({ trips, vehicle }) {
  const [readings, setReadings] = useState([]);
  const [value, setValue] = useState("");
  const [status, setStatus] = useState("");

  // Matarstallningen ar bilens, inte flottans: avlasningar och berakning
  // galler den valda bilen.
  const load = async () => {
    if (!vehicle) { setReadings([]); return; }
    const { data } = await supabase
      .from("drive_odometer").select("*").eq("vehicle_id", vehicle.id)
      .order("read_at", { ascending: false })
      .limit(5);
    setReadings(data ?? []);
  };
  // eslint-disable-next-line react-hooks/exhaustive-deps
  useEffect(() => { load(); }, [vehicle?.id]);

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
      .from("drive_odometer").insert({ odometer_km: km, vehicle_id: vehicle?.id ?? null });
    if (error) { setStatus(error.message); return; }
    setValue("");
    setStatus("avstämd");
    load();
  };

  return (
    <div className="card">
      <h2>Mätarställning{vehicle ? ` – ${vehicleLabel(vehicle)}` : ""}</h2>
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
  const [vehicles, setVehicles] = useState([]);
  const [vehicleFilter, setVehicleFilter] = useState("alla");
  const [cardCount, setCardCount] = useState(6);
  const [selected, setSelected] = useState(null);
  const [status, setStatus] = useState("hämtar …");

  const load = async () => {
    const [t, c, v] = await Promise.all([
      supabase.from("drive_trips").select("*")
        .order("start_utc", { ascending: false }).limit(500),
      supabase.from("drive_customers").select("*").eq("active", true)
        .order("name"),
      supabase.from("drive_vehicles").select("*").eq("active", true)
        .order("id"),
    ]);
    if (t.error) { setStatus(t.error.message); return; }
    setTrips(t.data ?? []);
    setCustomers(c.data ?? []);
    setVehicles(v.data ?? []);
    setStatus(t.data?.length ? "" : "Inga resor än – börja under Importera.");
  };
  useEffect(() => { load(); }, []);

  // Filtret galler allt pa sidan: minikorten, overblicken och tabellen.
  const shown = useMemo(() => {
    if (vehicleFilter === "alla") return trips;
    return trips.filter((t) => tripVehicleId(t, vehicles) === Number(vehicleFilter));
  }, [trips, vehicles, vehicleFilter]);

  // Matarstallningen ar alltid en bils: den valda, eller forsta bilen nar
  // hela flottan visas.
  const odoVehicle = vehicleFilter === "alla"
    ? vehicles[0]
    : vehicles.find((v) => v.id === Number(vehicleFilter)) ?? vehicles[0];
  const odoTrips = useMemo(
    () => (odoVehicle
      ? trips.filter((t) => tripVehicleId(t, vehicles) === odoVehicle.id)
      : []),
    [trips, vehicles, odoVehicle],
  );

  const patch = async (id, fields) => {
    setTrips((xs) => xs.map((t) => (t.id === id ? { ...t, ...fields } : t)));
    setSelected((sel) => (sel && sel.id === id ? { ...sel, ...fields } : sel));
    const { error } = await supabase
      .from("drive_trips").update(fields).eq("id", id);
    if (error) setStatus(`kunde inte spara: ${error.message}`);
  };

  const totals = useMemo(() => {
    const km = shown.reduce((a, t) => a + (t.distance_m || 0), 0) / 1000;
    const per = { privat: 0, foretag: 0, diffust: 0, omarkt: 0 };
    for (const t of shown) per[t.purpose ?? "omarkt"] += (t.distance_m || 0) / 1000;
    const moving = shown.reduce((a, t) => a + (t.moving_s || 0), 0);
    // Osignerade resor ar hal i bevisningen om journalen nagonsin granskas -
    // de raknas har sa att de blir atgardade i tid, inte upptackta i efterhand.
    const unsigned = shown.filter(
      (t) => !t.purpose || t.purpose === "omarkt" || t.purpose === "diffust",
    ).length;
    return { km, per, moving, unsigned };
  }, [shown]);

  const exportCsv = () => {
    const rows = [[
      "resa", "start", "mal", "start_plats", "mal_plats", "km", "syfte",
      "kund", "maxfart_kmh", "fortkorning_min", "ecopoang",
      "matarstallning_km", "anteckning",
    ].join(";")];
    for (const t of [...shown].reverse()) {
      rows.push([
        t.trip_no, fmtDateTime(t.start_utc), fmtDateTime(t.end_utc),
        t.start_place ?? "", t.end_place ?? "",
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
      {vehicles.length > 1 && (
        <div className="filters" style={{ marginBottom: ".8rem" }}>
          <button className={vehicleFilter === "alla" ? "active" : ""}
            onClick={() => setVehicleFilter("alla")}>Hela flottan</button>
          {vehicles.map((v) => (
            <button key={v.id}
              className={vehicleFilter === String(v.id) ? "active" : ""}
              onClick={() => setVehicleFilter(String(v.id))}>
              {vehicleLabel(v)}
            </button>
          ))}
        </div>
      )}

      <MiniCards trips={shown.slice(0, cardCount)}
        onOpen={(t) => setSelected(t)} />
      {shown.length > cardCount && (
        <p className="noprint" style={{ textAlign: "center", marginTop: "-.3rem" }}>
          <button className="ghost"
            onClick={() => setCardCount((n) => n + 12)}>
            Ladda fler ({shown.length - cardCount} kvar)
          </button>
        </p>
      )}

      <div className="card">
        <h2>Totalt Summerat</h2>
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

      <OdometerCard trips={odoTrips} vehicle={odoVehicle} />

      <div className="card">
        <h2>Resor</h2>
        <p className="status">{status}</p>
        <div style={{ overflowX: "auto" }}>
          <table className="journal">
            <thead>
              <tr>
                <th></th><th>Nr</th><th>Start</th><th>Mål</th><th>Km</th>
                <th>Syfte</th><th>Kund</th>
                {vehicles.length > 1 && <th>Bil</th>}
                <th>Mätare</th><th>Anteckning</th>
              </tr>
            </thead>
            <tbody>
              {shown.map((t) => (
                <tr key={t.id}>
                  <td><span className="chip"
                    style={{ background: PURPOSE_COLOR[t.purpose] ?? PURPOSE_COLOR.omarkt }} /></td>
                  <td>{t.trip_no}</td>
                  <td>
                    {fmtDateTime(t.start_utc)}
                    <div>
                      <PlacePicker lat={t.start_lat} lon={t.start_lon}
                        value={t.start_place}
                        onPick={(name) => patch(t.id, { start_place: name })} />
                    </div>
                  </td>
                  <td>
                    {fmtDateTime(t.end_utc)}
                    <div>
                      <PlacePicker lat={t.end_lat} lon={t.end_lon}
                        value={t.end_place}
                        onPick={(name) => patch(t.id, { end_place: name })} />
                    </div>
                  </td>
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
                  {vehicles.length > 1 && (
                    <td>
                      <select value={tripVehicleId(t, vehicles) ?? ""}
                        onChange={(e) => patch(t.id, {
                          vehicle_id: e.target.value ? Number(e.target.value) : null,
                        })}>
                        {vehicles.map((v) => (
                          <option key={v.id} value={v.id}>{vehicleLabel(v)}</option>
                        ))}
                      </select>
                    </td>
                  )}
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

      {selected && (
        <TripModal trip={selected} customers={customers} vehicles={vehicles}
          patch={patch} onClose={() => setSelected(null)} />
      )}
    </>
  );
}
