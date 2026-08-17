// Kartan: sparen ritade over OpenStreetMap, fargade efter syfte - eller som
// varmekarta, dar alla spar laggs halvgenomskinliga pa varandra och de vagar
// bilen faktiskt trafikerar lyser fram av sig sjalva.
import { useEffect, useRef, useState } from "react";
import L from "leaflet";
import "leaflet/dist/leaflet.css";
import { supabase, GPX_BUCKET } from "../lib/supabase.js";
import { parseGpx } from "../lib/gpx.js";
import { PURPOSE_COLOR } from "../lib/palette.js";
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
        const pts = parseGpx(await blob.text());
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
      const line = m === "varme"
        ? L.polyline(pts, { color: "#3c9dff", weight: 7, opacity: 0.07 })
        : L.polyline(pts, {
            color: PURPOSE_COLOR[trip.purpose] ?? PURPOSE_COLOR.omarkt,
            weight: 3, opacity: 0.75,
          }).bindTooltip(
            `Resa ${trip.trip_no} · ${fmtKm(trip.distance_m)} km · ` +
            `${purposeLabel(trip.purpose)} · ${fmtDate(trip.start_utc)}`,
          );
      line.addTo(layer);
      bounds = bounds ? bounds.extend(line.getBounds()) : line.getBounds();
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
        <button className={mode === "varme" ? "active" : ""}
          onClick={() => setMode("varme")}>Värmekarta</button>
      </div>
      {mode === "syfte" && (
        <div className="legend">
          {Object.entries({ privat: "Privat", foretag: "Företag", diffust: "Diffust" })
            .map(([k, label]) => (
              <span key={k}>
                <span className="sw" style={{ background: PURPOSE_COLOR[k] }} />
                {label}
              </span>
            ))}
        </div>
      )}
      <div ref={hostRef} className="map" />
      <p className="status">{status}</p>
    </div>
  );
}
