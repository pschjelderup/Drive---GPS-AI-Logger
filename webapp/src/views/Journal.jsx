// Korjournalen: resorna i tabell, redigerbara dar det behovs, och
// matarstallningen som raknas fram ur strackorna och stams av mot bilen.
import { useEffect, useMemo, useRef, useState } from "react";
import { supabase, GPX_BUCKET } from "../lib/supabase.js";
import {
  fmtKm, fmtDateTime, fmtDur, intFmt, kmFmt, PURPOSES, purposeLabel,
} from "../lib/fmt.js";
import { PURPOSE_COLOR } from "../lib/palette.js";
import { parseGpx, distanceM } from "../lib/gpx.js";
import { vehicleLabel, tripVehicleId, vehicleRate } from "../lib/vehicles.js";

// Kanns en kunds kontor igen i resans andpunkter? Inom 400 meter raknas det
// som ett besok - nara nog for en parkering, langt ifran slumptraffar.
function nearCustomer(t, customers) {
  let best = null;
  for (const c of customers) {
    if (c.lat == null || c.lon == null) continue;
    for (const [where, la, lo] of [
      ["start", t.start_lat, t.start_lon],
      ["mål", t.end_lat, t.end_lon],
    ]) {
      if (!la && !lo) continue;
      const d = distanceM(la, lo, c.lat, c.lon);
      if (d <= 400 && (!best || d < best.d)) best = { customer: c, where, d };
    }
  }
  return best;
}

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
  } else if (!pts || pts.filter(Boolean).length < 2) {
    content = <span className="mm-note">inget spår</span>;
  } else if (dim) {
    const { w, h } = dim;
    const merc = (lat, lon) => {
      const x = (lon + 180) / 360;
      const sn = Math.sin((lat * Math.PI) / 180);
      const y = 0.5 - Math.log((1 + sn) / (1 - sn)) / (4 * Math.PI);
      return [x, y];
    };
    // Grupperade resor skickar in sina delspar med null emellan - dar lyfts
    // pennan, sa att inga later-som-korda streck ritas mellan resorna.
    let minX = 1, maxX = 0, minY = 1, maxY = 0;
    for (const p of pts) {
      if (!p) continue;
      const [x, y] = merc(p[0], p[1]);
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
    let pen = false, n = 0;
    let first = null, last = null;
    for (const p of pts) {
      if (!p) { pen = false; continue; }
      n++;
      if (!first) first = p;
      last = p;
      if (pen && n % step !== 0) continue;
      d.push(`${pen ? "L" : "M"}${px(p[0], p[1])}`);
      pen = true;
    }
    d.push(`L${px(last[0], last[1])}`);
    const [sx, sy] = px(first[0], first[1]).split(",");
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

// Korten: en resa eller en grupp av resor per kort. Sex visas fran borjan
// och "Ladda fler" tar resten - sparen hamtas bara for kort som visas, och
// en grupps delspar sys ihop med lyft penna emellan. I markeringslaget
// valjer klicken i stallet ut resor att gruppera.
function MiniCards({ entries, onOpen, selMode, selIds, onToggle }) {
  const [tracks, setTracks] = useState({});
  const ids = entries.map((e) => e.key).join(",");

  useEffect(() => {
    let alive = true;
    (async () => {
      for (const e of entries) {
        if (e.key in tracks) continue;
        const paths = e.trips.map((t) => t.gpx_path).filter(Boolean);
        if (!paths.length) {
          setTracks((m) => (e.key in m ? m : { ...m, [e.key]: null }));
          continue;
        }
        const joined = [];
        for (const p of paths) {
          const { data } = await supabase.storage.from(GPX_BUCKET).download(p);
          const pts = data ? parseGpx(await data.text()) : null;
          if (pts?.length) {
            if (joined.length) joined.push(null);
            joined.push(...pts);
          }
        }
        if (!alive) return;
        setTracks((m) => ({ ...m, [e.key]: joined.length ? joined : null }));
      }
    })();
    return () => { alive = false; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [ids]);

  if (!entries.length) return null;

  return (
    <div className="minicards">
      {entries.map((e) => {
        const t = e.view;
        const color = PURPOSE_COLOR[t.purpose] ?? PURPOSE_COLOR.omarkt;
        const selectable = selMode && !e.isGroup;
        const sel = selectable && selIds.has(e.trips[0].id);
        return (
          <div key={e.key} role="button" tabIndex={0}
            className={`minicard${sel ? " sel" : ""}${selMode && e.isGroup ? " dis" : ""}`}
            onClick={() => (selectable ? onToggle(e.trips[0].id) : !selMode && onOpen?.(e))}
            onKeyDown={(ev) => ev.key === "Enter" &&
              (selectable ? onToggle(e.trips[0].id) : !selMode && onOpen?.(e))}>
            <div className="mc-accent" style={{ background: color }} />
            <div className="mc-head">
              <b>{e.isGroup
                ? (e.group.label || "Grupp")
                : `Resa ${t.trip_no}`}</b>
              <span>{fmtDateTime(t.start_utc)}</span>
            </div>
            {e.isGroup && (
              <div className="mc-badge">{e.trips.length} delresor</div>
            )}
            {selectable && <div className="mc-check">{sel ? "✓" : ""}</div>}
            <TrackMini pts={tracks[e.key]} color={color} />
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
              {t.purpose === "blandat" ? "Blandat" : purposeLabel(t.purpose)}
              {t.customer ? ` · ${t.customer}` : ""}
              {t.speeding_s ? ` · ${Math.round(t.speeding_s / 60)} min över gränsen` : ""}
            </div>
            {(t.start_place || t.end_place || t.start_addr || t.end_addr) && (
              <div className="mc-foot">
                {t.start_place ?? t.start_addr ?? "…"} → {t.end_place ?? t.end_addr ?? "…"}
              </div>
            )}
          </div>
        );
      })}
    </div>
  );
}

// Flera resor sedda som en: summorna ar summor, tiderna ar forsta start och
// sista mal, och det som inte ar entydigt (syfte, kund) sags vara blandat i
// stallet for att gissas.
function aggregateGroup(group, members) {
  const sorted = [...members].sort(
    (a, b) => new Date(a.start_utc) - new Date(b.start_utc),
  );
  const sumOr = (get) => {
    let any = false, sum = 0;
    for (const t of sorted) {
      const v = get(t);
      if (v != null) { any = true; sum += v; }
    }
    return any ? sum : null;
  };
  const uniform = (get) => {
    const vals = new Set(sorted.map(get).filter((v) => v != null && v !== ""));
    return vals.size === 1 ? [...vals][0] : null;
  };
  const km = sorted.reduce((a, t) => a + (t.distance_m || 0), 0);
  let ecoW = 0, ecoSum = 0;
  for (const t of sorted) {
    if (t.eco_score != null && t.distance_m) {
      ecoW += t.distance_m;
      ecoSum += t.eco_score * t.distance_m;
    }
  }
  return {
    trip_no: sorted.map((t) => t.trip_no).join("+"),
    start_utc: sorted[0]?.start_utc,
    end_utc: sorted[sorted.length - 1]?.end_utc,
    distance_m: km,
    moving_s: sumOr((t) => t.moving_s),
    speeding_s: sumOr((t) => t.speeding_s),
    max_speed_kmh: Math.max(0, ...sorted.map((t) => t.max_speed_kmh || 0)) || null,
    eco_score: ecoW > 0 ? ecoSum / ecoW : null,
    hard_events: sumOr((t) => t.hard_events),
    purpose: uniform((t) => t.purpose) ?? "blandat",
    customer: uniform((t) => t.customer) ?? "",
    start_place: sorted[0]?.start_place ?? null,
    end_place: sorted[sorted.length - 1]?.end_place ?? null,
    start_addr: sorted[0]?.start_addr ?? null,
    end_addr: sorted[sorted.length - 1]?.end_addr ?? null,
  };
}

// Gruppkortet: helheten overst, delstrackorna under - var och en klickbar.
function GroupModal({ entry, onClose, onOpenTrip, onUngroup, onRelabel }) {
  const t = entry.view;
  const kv = (label, value) => (
    <div className="kv"><span>{label}</span><b>{value}</b></div>
  );
  return (
    <div className="modal-scrim" onClick={onClose}>
      <div className="modal card" onClick={(e) => e.stopPropagation()}>
        <div className="mc-head" style={{ marginBottom: ".5rem" }}>
          <input type="text" defaultValue={entry.group.label ?? ""}
            placeholder="namn på gruppen" style={{ fontWeight: 600 }}
            onBlur={(e) => onRelabel(entry.group.id, e.target.value || null)} />
          <button className="ghost" onClick={onClose}>stäng</button>
        </div>
        <div className="kvgrid" style={{ marginBottom: ".6rem" }}>
          {kv("Start", fmtDateTime(t.start_utc))}
          {kv("Mål", fmtDateTime(t.end_utc))}
          {kv("Sträcka", `${fmtKm(t.distance_m)} km`)}
          {kv("Rullande tid", t.moving_s != null ? fmtDur(t.moving_s) : "–")}
          {kv("Toppfart", t.max_speed_kmh ? `${Math.round(t.max_speed_kmh)} km/h` : "–")}
          {kv("Ecopoäng", t.eco_score != null ? Math.round(t.eco_score) : "omätt")}
        </div>
        <h2 style={{ fontSize: ".85rem", color: "var(--dim)", textTransform: "uppercase" }}>
          Delsträckor
        </h2>
        <table className="journal">
          <tbody>
            {entry.trips.map((m) => (
              <tr key={m.id} style={{ cursor: "pointer" }}
                onClick={() => onOpenTrip(m)}>
                <td><span className="chip"
                  style={{ background: PURPOSE_COLOR[m.purpose] ?? PURPOSE_COLOR.omarkt }} /></td>
                <td>Resa {m.trip_no}</td>
                <td>{fmtDateTime(m.start_utc)}</td>
                <td>{fmtKm(m.distance_m)} km</td>
                <td>{purposeLabel(m.purpose)}</td>
                <td>{m.customer ?? ""}</td>
              </tr>
            ))}
          </tbody>
        </table>
        <p style={{ marginTop: ".8rem" }}>
          <button className="ghost" onClick={() => onUngroup(entry.group.id)}>
            Ta isär gruppen
          </button>
        </p>
      </div>
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
  const near = nearCustomer(t, customers);
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

        {near && near.customer.name !== t.customer && (
          <p className="suggest">
            Resan {near.where === "start" ? "startade" : "slutade"}{" "}
            {Math.round(near.d)} m från <b>{near.customer.name}</b>.{" "}
            <button className="ghost mini" onClick={() => patch(t.id, {
              customer: near.customer.name,
              purpose: "foretag",
              ...(near.where === "start"
                ? { start_place: near.customer.name }
                : { end_place: near.customer.name }),
            })}>
              sätt som kund och {near.where === "start" ? "startplats" : "målplats"}
            </button>
          </p>
        )}

        <div className="kvgrid">
          {kv("Start", <>
            {fmtDateTime(t.start_utc)}
            {t.start_lat ? <>{" "}
              <a href={maps(t.start_lat, t.start_lon)} target="_blank"
                rel="noreferrer">karta</a></> : null}
            {t.start_addr ? <div className="addr">{t.start_addr}</div> : null}
          </>)}
          {kv("Mål", <>
            {fmtDateTime(t.end_utc)}
            {t.end_lat ? <>{" "}
              <a href={maps(t.end_lat, t.end_lon)} target="_blank"
                rel="noreferrer">karta</a></> : null}
            {t.end_addr ? <div className="addr">{t.end_addr}</div> : null}
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
  const [period, setPeriod] = useState("alla");
  const [groups, setGroups] = useState([]);
  const [places, setPlaces] = useState([]);
  const [billRate, setBillRate] = useState(null);
  const [cardCount, setCardCount] = useState(6);
  const [selected, setSelected] = useState(null);
  const [openGroup, setOpenGroup] = useState(null);
  const [selMode, setSelMode] = useState(false);
  const [selIds, setSelIds] = useState(new Set());
  const [groupName, setGroupName] = useState("");
  const [status, setStatus] = useState("hämtar …");

  const load = async () => {
    const [t, c, v, g, st, pl] = await Promise.all([
      supabase.from("drive_trips").select("*")
        .order("start_utc", { ascending: false }).limit(500),
      supabase.from("drive_customers").select("*").eq("active", true)
        .order("name"),
      supabase.from("drive_vehicles").select("*").eq("active", true)
        .order("id"),
      supabase.from("drive_trip_groups").select("*"),
      supabase.from("drive_settings").select("*")
        .eq("key", "debiterat_per_mil").maybeSingle(),
      supabase.from("drive_places").select("*"),
    ]);
    if (t.error) { setStatus(t.error.message); return; }
    setTrips(t.data ?? []);
    setCustomers(c.data ?? []);
    setVehicles(v.data ?? []);
    setGroups(g.data ?? []);
    setPlaces(pl.data ?? []);
    const raw = st.data?.value;
    setBillRate(typeof raw === "number" ? raw : parseFloat(raw) || null);
    setStatus(t.data?.length ? "" : "Inga resor än – börja under Importera.");
  };
  useEffect(() => { load(); }, []);

  // Adresserna fylls i i efterhand: resorna fran enheten bar bara koordinater,
  // och har slas gatuadressen upp och sparas pa raden - en gang per resa,
  // sedan star den i databasen. Hogst ett par dussin uppslag per sidladdning;
  // resten tas nasta gang, sa en stor efterslapning aldrig blir en dyr sida.
  const geocodedRef = useRef(false);
  useEffect(() => {
    if (geocodedRef.current || !trips.length) return;
    geocodedRef.current = true;
    (async () => {
      const { data: { session } } = await supabase.auth.getSession();
      const token = session?.access_token;
      if (!token) return;
      const lookup = async (lat, lon) => {
        try {
          const r = await fetch(`/api/geocode?lat=${lat}&lon=${lon}`, {
            headers: { Authorization: `Bearer ${token}` },
          });
          if (!r.ok) return null;
          return (await r.json()).address ?? null;
        } catch {
          return null;
        }
      };
      const hasPos = (la, lo) =>
        Number.isFinite(la) && Number.isFinite(lo) && (la || lo);

      // Egna platser forst: en resa som borjar eller slutar inom 400 meter
      // fran hemmet eller kontoret far platsens namn - "Hemma -> Kontoret"
      // i stallet for tva namnlosa koordinater. Bara tomma falt fylls i;
      // det nagon valt sjalv ror vi aldrig.
      const nearOwn = (la, lo) => {
        let best = null;
        for (const p of places) {
          if (p.lat == null || p.lon == null) continue;
          const d = distanceM(la, lo, p.lat, p.lon);
          if (d <= 400 && (!best || d < best.d)) best = { p, d };
        }
        return best?.p ?? null;
      };
      for (const t of trips) {
        const fields = {};
        if (!t.start_place && hasPos(t.start_lat, t.start_lon)) {
          const p = nearOwn(t.start_lat, t.start_lon);
          if (p) fields.start_place = p.label;
        }
        if (!t.end_place && hasPos(t.end_lat, t.end_lon)) {
          const p = nearOwn(t.end_lat, t.end_lon);
          if (p) fields.end_place = p.label;
        }
        if (Object.keys(fields).length) await patch(t.id, fields);
      }

      let budget = 40;
      for (const t of trips) {
        if (budget <= 0) break;
        const fields = {};
        if (!t.start_addr && hasPos(t.start_lat, t.start_lon)) {
          budget--;
          const a = await lookup(t.start_lat, t.start_lon);
          if (a) fields.start_addr = a;
        }
        if (!t.end_addr && hasPos(t.end_lat, t.end_lon)) {
          budget--;
          const a = await lookup(t.end_lat, t.end_lon);
          if (a) fields.end_addr = a;
        }
        if (Object.keys(fields).length) await patch(t.id, fields);
      }
    })();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [trips]);

  // Filtren galler allt pa sidan: minikorten, summeringen och tabellen.
  // Perioderna ar kalenderns, inte rullande dygn - det ar sa en korjournal
  // last: idag, den har veckan, den har manaden, det har aret.
  const periodStart = useMemo(() => {
    const now = new Date();
    const d = new Date(now.getFullYear(), now.getMonth(), now.getDate());
    switch (period) {
      case "dag": return d;
      case "vecka": {
        const wd = (d.getDay() + 6) % 7;  // mandag = 0
        d.setDate(d.getDate() - wd);
        return d;
      }
      case "manad": return new Date(now.getFullYear(), now.getMonth(), 1);
      case "ar": return new Date(now.getFullYear(), 0, 1);
      default: return null;
    }
  }, [period]);

  const shown = useMemo(() => {
    let xs = trips;
    if (vehicleFilter !== "alla") {
      xs = xs.filter((t) => tripVehicleId(t, vehicles) === Number(vehicleFilter));
    }
    if (periodStart) {
      xs = xs.filter((t) => t.start_utc && new Date(t.start_utc) >= periodStart);
    }
    return xs;
  }, [trips, vehicles, vehicleFilter, periodStart]);

  // Korten: grupperade resor blir en post, resten star for sig sjalva.
  const entries = useMemo(() => {
    const byGroup = new Map();
    const singles = [];
    for (const t of shown) {
      if (t.group_id) {
        if (!byGroup.has(t.group_id)) byGroup.set(t.group_id, []);
        byGroup.get(t.group_id).push(t);
      } else {
        singles.push({ key: String(t.id), isGroup: false, trips: [t], view: t });
      }
    }
    const grouped = [];
    for (const [gid, members] of byGroup) {
      const group = groups.find((g) => g.id === gid) ?? { id: gid, label: null };
      grouped.push({
        key: `g${gid}`, isGroup: true, group, trips: members,
        view: aggregateGroup(group, members),
      });
    }
    return [...singles, ...grouped].sort(
      (a, b) => new Date(b.view.start_utc) - new Date(a.view.start_utc),
    );
  }, [shown, groups]);

  // ---- grupperingen
  const toggleSel = (id) => setSelIds((old) => {
    const next = new Set(old);
    if (next.has(id)) next.delete(id);
    else next.add(id);
    return next;
  });

  const selectWholeDays = () => {
    const days = new Set([...selIds].map((id) => {
      const t = shown.find((x) => x.id === id);
      return t ? new Date(t.start_utc).toDateString() : null;
    }).filter(Boolean));
    setSelIds(new Set(shown
      .filter((t) => !t.group_id &&
        days.has(new Date(t.start_utc).toDateString()))
      .map((t) => t.id)));
  };

  const createGroup = async () => {
    const ids = [...selIds];
    if (ids.length < 2) return;
    const first = shown.find((t) => t.id === ids[0]);
    // Namnet ar anvandarens; utan namn far gruppen dagens datum.
    const label = groupName.trim() ||
      (first ? new Date(first.start_utc).toLocaleDateString("sv-SE") : null);
    const { data, error } = await supabase
      .from("drive_trip_groups").insert({ label }).select().single();
    if (error) { setStatus(error.message); return; }
    await supabase.from("drive_trips")
      .update({ group_id: data.id }).in("id", ids);
    setSelMode(false);
    setSelIds(new Set());
    setGroupName("");
    load();
  };

  const ungroup = async (gid) => {
    await supabase.from("drive_trips")
      .update({ group_id: null }).eq("group_id", gid);
    await supabase.from("drive_trip_groups").delete().eq("id", gid);
    setOpenGroup(null);
    load();
  };

  const relabelGroup = async (gid, label) => {
    await supabase.from("drive_trip_groups").update({ label }).eq("id", gid);
    setGroups((gs) => gs.map((g) => (g.id === gid ? { ...g, label } : g)));
  };

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

    // Milersattningen raknas per bil - satsen beror pa bilens typ.
    let ersattning = 0;
    for (const v of vehicles) {
      const vkm = shown
        .filter((t) => t.purpose === "foretag" && tripVehicleId(t, vehicles) === v.id)
        .reduce((a, t) => a + (t.distance_m || 0), 0) / 1000;
      ersattning += (vkm / 10) * vehicleRate(v);
    }
    return { km, per, moving, unsigned, ersattning };
  }, [shown, vehicles]);

  // Fakturerbart, per kund: kundens eget pris per mil vinner, det allmanna
  // priset ur installningarna ar reserv. Foretagsresor utan kund far det
  // allmanna priset - eller star som ovarderade om inget finns.
  const perKund = useMemo(() => {
    const rows = new Map();
    for (const t of shown) {
      if (t.purpose !== "foretag") continue;
      const name = t.customer || "– utan kund –";
      if (!rows.has(name)) rows.set(name, { name, trips: 0, km: 0 });
      const r = rows.get(name);
      r.trips++;
      r.km += (t.distance_m || 0) / 1000;
    }
    const out = [...rows.values()].map((r) => {
      const c = customers.find((x) => x.name === r.name);
      const rate = c?.rate_per_mil ?? billRate;
      return { ...r, rate, kr: rate != null ? (r.km / 10) * rate : null };
    }).sort((a, b) => b.km - a.km);
    const totalKr = out.reduce((a, r) => a + (r.kr ?? 0), 0);
    const allPriced = out.every((r) => r.kr != null);
    return { rows: out, totalKr, allPriced };
  }, [shown, customers, billRate]);

  const exportCsv = () => {
    const rows = [[
      "resa", "start", "mal", "start_plats", "mal_plats",
      "start_adress", "mal_adress", "km", "syfte",
      "kund", "maxfart_kmh", "fortkorning_min", "ecopoang",
      "matarstallning_km", "anteckning",
    ].join(";")];
    for (const t of [...shown].reverse()) {
      rows.push([
        t.trip_no, fmtDateTime(t.start_utc), fmtDateTime(t.end_utc),
        t.start_place ?? "", t.end_place ?? "",
        (t.start_addr ?? "").replaceAll(";", ","),
        (t.end_addr ?? "").replaceAll(";", ","),
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

  const periods = [
    ["alla", "Allt"], ["dag", "Idag"], ["vecka", "Vecka"],
    ["manad", "Månad"], ["ar", "År"],
  ];

  return (
    <>
      <div className="filters" style={{ marginBottom: ".5rem" }}>
        {periods.map(([k, label]) => (
          <button key={k} className={period === k ? "active" : ""}
            onClick={() => setPeriod(k)}>{label}</button>
        ))}
        {vehicles.length > 1 && <span style={{ width: ".6rem" }} />}
        {vehicles.length > 1 && (
          <button className={vehicleFilter === "alla" ? "active" : ""}
            onClick={() => setVehicleFilter("alla")}>Hela flottan</button>
        )}
        {vehicles.length > 1 && vehicles.map((v) => (
          <button key={v.id}
            className={vehicleFilter === String(v.id) ? "active" : ""}
            onClick={() => setVehicleFilter(String(v.id))}>
            {vehicleLabel(v)}
          </button>
        ))}
      </div>

      <div className="filters" style={{ marginBottom: ".8rem" }}>
        {!selMode ? (
          <button onClick={() => { setSelMode(true); setSelIds(new Set()); }}>
            Gruppera resor …
          </button>
        ) : (
          <>
            <span className="status" style={{ alignSelf: "center" }}>
              {selIds.size} valda – klicka på korten
            </span>
            <button onClick={selectWholeDays} disabled={!selIds.size}>
              Välj hela dagen
            </button>
            <input type="text" placeholder="namn på gruppen (valfritt)"
              value={groupName} style={{ width: "14rem" }}
              onChange={(e) => setGroupName(e.target.value)}
              onKeyDown={(e) => e.key === "Enter" && selIds.size >= 2 && createGroup()} />
            <button className="active" onClick={createGroup}
              disabled={selIds.size < 2}>
              Skapa grupp
            </button>
            <button onClick={() => { setSelMode(false); setSelIds(new Set()); }}>
              Avbryt
            </button>
          </>
        )}
      </div>

      <MiniCards entries={entries.slice(0, cardCount)}
        selMode={selMode} selIds={selIds} onToggle={toggleSel}
        onOpen={(e) => (e.isGroup ? setOpenGroup(e) : setSelected(e.trips[0]))} />
      {entries.length > cardCount && (
        <p className="noprint" style={{ textAlign: "center", marginTop: "-.3rem" }}>
          <button className="ghost"
            onClick={() => setCardCount((n) => n + 12)}>
            Ladda fler ({entries.length - cardCount} kvar)
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
          <div className="tile">
            <b>{intFmt.format(Math.round(totals.ersattning))} kr</b>
            <span>milersättning (företagsmil × bilens sats)</span>
          </div>
          <div className="tile">
            <b>{perKund.rows.length
              ? `${intFmt.format(Math.round(perKund.totalKr))} kr${perKund.allPriced ? "" : " *"}`
              : "–"}</b>
            <span>fakturerbart till kund</span>
          </div>
        </div>
        {!perKund.allPriced && perKund.rows.length > 0 && (
          <p className="status" style={{ marginTop: ".5rem", marginBottom: 0 }}>
            * någon kund saknar pris per mil – sätt det under Inställningar.
          </p>
        )}
      </div>

      {perKund.rows.length > 0 && (
        <div className="card">
          <h2>Per kund</h2>
          <table className="journal">
            <thead>
              <tr><th>Kund</th><th>Resor</th><th>Km</th><th>Kr/mil</th><th>Fakturerbart</th></tr>
            </thead>
            <tbody>
              {perKund.rows.map((r) => (
                <tr key={r.name}>
                  <td>{r.name}</td>
                  <td>{r.trips}</td>
                  <td>{kmFmt.format(r.km)}</td>
                  <td>{r.rate != null ? kmFmt.format(r.rate) : "–"}</td>
                  <td>{r.kr != null
                    ? `${intFmt.format(Math.round(r.kr))} kr` : "–"}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}

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
                    {t.start_addr && <div className="addr">{t.start_addr}</div>}
                    <div>
                      <PlacePicker lat={t.start_lat} lon={t.start_lon}
                        value={t.start_place}
                        onPick={(name) => patch(t.id, { start_place: name })} />
                    </div>
                  </td>
                  <td>
                    {fmtDateTime(t.end_utc)}
                    {t.end_addr && <div className="addr">{t.end_addr}</div>}
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
                    {!t.customer && (() => {
                      const near = nearCustomer(t, customers);
                      return near ? (
                        <div>
                          <button className="ghost mini"
                            onClick={() => patch(t.id, {
                              customer: near.customer.name, purpose: "foretag",
                            })}>
                            → {near.customer.name}?
                          </button>
                        </div>
                      ) : null;
                    })()}
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
      {openGroup && (
        <GroupModal entry={openGroup}
          onClose={() => setOpenGroup(null)}
          onOpenTrip={(t) => { setOpenGroup(null); setSelected(t); }}
          onUngroup={ungroup}
          onRelabel={relabelGroup} />
      )}
    </>
  );
}
