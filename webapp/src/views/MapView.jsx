// Kartan: sparen ritade over OpenStreetMap i tre lagen - fargade efter syfte,
// som varmekarta dar de vagar bilen faktiskt trafikerar lyser fram, eller
// fargade efter fart langs sparet.
//
// Kartfargerna ar inte diagramfargerna: kartan ar sjalv gron och beige, sa
// sparen ritas i djupbla, magenta och brand orange - farger som inte finns i
// kartbilden (validerade mot OSM-beige yta, aven for fargblinda).
import { useEffect, useRef, useState } from "react";
import L from "leaflet";
import "leaflet/dist/leaflet.css";
import { supabase, GPX_BUCKET } from "../lib/supabase.js";
import { parseGpxTimed, segmentSpeedKmh } from "../lib/gpx.js";
import { MAP_COLOR, MAP_HEAT, SPEED_BINS, speedColor } from "../lib/palette.js";
import { purposeLabel, fmtKm, fmtDate } from "../lib/fmt.js";

export default function MapView() {
  const hostRef = useRef(null);
  const mapRef = useRef(null);
  const layerRef = useRef(null);
  const tracksRef = useRef(null); // [{trip, pts}] nar de val ar hamtade
  const [mode, setMode] = useState("syfte");
  const [status, setStatus] = useState("hämtar spår …");

  useEffect(() => {
    const map = L.map(hostRef.current).setView([59.33, 18.06], 6);
    L.tileLayer("https://tile.openstreetmap.org/{z}/{x}/{y}.png", {
      maxZoom: 19,
      attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a>',
    }).addTo(map);
    mapRef.current = map;
    layerRef.current = L.layerGroup().addTo(map);

    (async () => {
      const { data: trips, error } = await supabase
        .from("drive_trips").select("id, trip_no, purpose, distance_m, start_utc, gpx_path")
        .not("gpx_path", "is", null)
        .order("start_utc", { ascending: false }).limit(200);
      if (error) { setStatus(error.message); return; }
      if (!trips?.length) {
        setStatus("Inga spår uppladdade än – GPX-filerna följer med under Importera.");
        return;
      }

      const tracks = [];
      for (const t of trips) {
        const { data: blob } = await supabase.storage
          .from(GPX_BUCKET).download(t.gpx_path);
        if (!blob) continue;
        const pts = parseGpxTimed(await blob.text());
        if (pts?.length > 1) tracks.push({ trip: t, pts });
        setStatus(`hämtar spår … ${tracks.length}/${trips.length}`);
      }
      tracksRef.current = tracks;
      setStatus(tracks.length ? "" : "Spåren gick inte att läsa.");
      draw(mode);
    })();

    return () => map.remove();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const draw = (m) => {
    const layer = layerRef.current;
    const tracks = tracksRef.current;
    if (!layer || !tracks?.length) return;
    layer.clearLayers();

    let bounds = null;
    for (const { trip, pts } of tracks) {
      const latlngs = pts.map((p) => [p.lat, p.lon]);
      const tip =
        `Resa ${trip.trip_no} · ${fmtKm(trip.distance_m)} km · ` +
        `${purposeLabel(trip.purpose)} · ${fmtDate(trip.start_utc)}`;

      if (m === "varme") {
        const line = L.polyline(latlngs, {
          color: MAP_HEAT, weight: 7, opacity: 0.07,
        });
        line.addTo(layer);
        bounds = bounds ? bounds.extend(line.getBounds()) : line.getBounds();
      } else if (m === "fart") {
        // En polylinje per sammanhangande fartklass, inte en per segment -
        // tusentals smalinjer skulle segla ifran Leaflet.
        let runPts = [latlngs[0]];
        let runColor = null;
        const flush = () => {
          if (runPts.length < 2) return;
          const line = L.polyline(runPts, {
            color: runColor ?? MAP_COLOR.omarkt,
            weight: 4, opacity: 0.85,
          }).bindTooltip(tip);
          line.addTo(layer);
          bounds = bounds ? bounds.extend(line.getBounds()) : line.getBounds();
        };
        for (let i = 1; i < pts.length; i++) {
          const kmh = segmentSpeedKmh(pts[i - 1], pts[i]);
          const c = kmh == null ? MAP_COLOR.omarkt : speedColor(kmh);
          if (runColor === null || c === runColor) {
            runColor = c;
            runPts.push(latlngs[i]);
          } else {
            flush();
            runPts = [latlngs[i - 1], latlngs[i]];
            runColor = c;
          }
        }
        flush();
      } else {
        const line = L.polyline(latlngs, {
          color: MAP_COLOR[trip.purpose] ?? MAP_COLOR.omarkt,
          weight: 3, opacity: 0.8,
        }).bindTooltip(tip);
        line.addTo(layer);
        bounds = bounds ? bounds.extend(line.getBounds()) : line.getBounds();
      }
    }
    if (bounds) mapRef.current.fitBounds(bounds, { padding: [24, 24] });
  };

  useEffect(() => { draw(mode); }, [mode]);

  return (
    <div className="card">
      <h2>Karta</h2>
      <div className="filters">
        <button className={mode === "syfte" ? "active" : ""}
          onClick={() => setMode("syfte")}>Färg efter syfte</button>
        <button className={mode === "fart" ? "active" : ""}
          onClick={() => setMode("fart")}>Färg efter fart</button>
        <button className={mode === "varme" ? "active" : ""}
          onClick={() => setMode("varme")}>Värmekarta</button>
      </div>
      {mode === "syfte" && (
        <div className="legend">
          {Object.entries({ privat: "Privat", foretag: "Företag", diffust: "Diffust" })
            .map(([k, label]) => (
              <span key={k}>
                <span className="sw" style={{ background: MAP_COLOR[k] }} />
                {label}
              </span>
            ))}
        </div>
      )}
      {mode === "fart" && (
        <div className="legend">
          {SPEED_BINS.map((b) => (
            <span key={b.label}>
              <span className="sw" style={{ background: b.color }} />
              {b.label} km/h
            </span>
          ))}
          <span>
            <span className="sw" style={{ background: MAP_COLOR.omarkt }} />
            fart okänd
          </span>
        </div>
      )}
      <div ref={hostRef} className="map" />
      <p className="status">{status}</p>
    </div>
  );
}
