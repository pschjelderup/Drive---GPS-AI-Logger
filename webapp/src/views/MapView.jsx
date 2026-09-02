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
import { parseKamerorBin, parseHastighetBin } from "../lib/trv.js";
import { createRoadLayer } from "../lib/roadlayer.js";
import {
  MAP_COLOR, MAP_HEAT, SPEED_BINS, speedColor,
  LIMIT_BINS, limitColor, CAM_RING, CAM_FILL,
} from "../lib/palette.js";
import { purposeLabel, fmtKm, fmtDate } from "../lib/fmt.js";

export default function MapView({ visible = true }) {
  const hostRef = useRef(null);
  const mapRef = useRef(null);
  const layerRef = useRef(null);
  const tracksRef = useRef(null); // [{trip, pts}] nar de val ar hamtade
  const camLayerRef = useRef(null);
  const roadLayerRef = useRef(null);
  const [mode, setMode] = useState("syfte");
  const modeRef = useRef("syfte");
  modeRef.current = mode;
  const [showGpx, setShowGpx] = useState(true);
  const [showCams, setShowCams] = useState(false);
  const [showRoads, setShowRoads] = useState(false);
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

      // Sparen hamtas sex at gangen och ritas in efterhand som de kommer,
      // i stallet for ett i taget och allt pa en gang i slutet. Tvahundra
      // filer i foljd tog dryga halvminuten innan kartan visade nagot; nu
      // syns de forsta sparen inom nagon sekund och resten fylls pa.
      const tracks = [];
      tracksRef.current = tracks;
      let bounds = null;
      let done = 0, next = 0, fitted = false;
      const worker = async () => {
        while (next < trips.length) {
          const t = trips[next++];
          try {
            const { data: blob } = await supabase.storage
              .from(GPX_BUCKET).download(t.gpx_path);
            if (blob) {
              const pts = parseGpxTimed(await blob.text());
              if (pts?.length > 1) {
                const entry = { trip: t, pts };
                tracks.push(entry);
                if (mapRef.current) {
                  bounds = drawTrack(layerRef.current, entry, modeRef.current, bounds);
                  // Zooma in pa det som finns sa fort nagot finns, och en
                  // gang till nar allt ar pa plats.
                  if (!fitted && tracks.length >= 8 && bounds) {
                    fitted = true;
                    mapRef.current.fitBounds(bounds, { padding: [24, 24] });
                  }
                }
              }
            }
          } catch { /* ett spar som inte gar att hamta hoppas over */ }
          done++;
          setStatus(`hämtar spår … ${done}/${trips.length}`);
        }
      };
      await Promise.all(
        Array.from({ length: Math.min(6, trips.length) }, worker));
      if (!mapRef.current) return;
      setStatus(tracks.length ? "" : "Spåren gick inte att läsa.");
      if (bounds) mapRef.current.fitBounds(bounds, { padding: [24, 24] });
    })();

    return () => { map.remove(); mapRef.current = null; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Fliken goms i stallet for att avmonteras. Leaflet mater sin ruta bara
  // nar den ritas, sa efter en dold period - och ett eventuellt fonsterbyte
  // under den - maste den fa veta att matten kan ha andrats.
  useEffect(() => {
    if (visible && mapRef.current) mapRef.current.invalidateSize();
  }, [visible]);

  const downloadData = async (key) => {
    const { data, error } = await supabase.storage.from("drive-data").download(key);
    if (error || !data) throw new Error(`${key}: ${error?.message ?? "saknas i molnet"}`);
    return data.arrayBuffer();
  };

  // De tre lagren ar oberoende: sparen, kamerorna och vagnatet slas pa och av
  // var for sig. Kamerorna och vagarna ar samma binarfiler som enheten kor pa,
  // hamtade ur molnet forsta gangen lagret slas pa och aterbrukade darefter.
  const toggleGpx = (on) => {
    setShowGpx(on);
    const l = layerRef.current;
    if (!l) return;
    if (on) l.addTo(mapRef.current);
    else l.remove();
  };

  const toggleCams = async (on) => {
    setShowCams(on);
    if (!on) { camLayerRef.current?.remove(); return; }
    if (camLayerRef.current) { camLayerRef.current.addTo(mapRef.current); return; }
    try {
      setStatus("hämtar kamerorna ur molnet …");
      const cams = parseKamerorBin(await downloadData("KAMEROR.BIN"));
      if (!cams) throw new Error("KAMEROR.BIN gick inte att läsa");
      // Egen canvas: tvatusen dom-noder hade markts, tvatusen cirklar pa en
      // canvas marks inte.
      const renderer = L.canvas({ padding: 0.2 });
      const group = L.layerGroup(cams.map((c) =>
        L.circleMarker([c.lat, c.lon], {
          renderer, radius: 5, color: CAM_RING, weight: 2.5,
          fillColor: CAM_FILL, fillOpacity: 1,
        }).bindTooltip(c.limit ? `Fartkamera · ${c.limit} km/h` : "Fartkamera")));
      camLayerRef.current = group.addTo(mapRef.current);
      setStatus(`${cams.length} kameror i lagret`);
    } catch (e) {
      setStatus(`fel: ${e.message}`);
      setShowCams(false);
    }
  };

  const toggleRoads = async (on) => {
    setShowRoads(on);
    if (!on) { roadLayerRef.current?.remove(); return; }
    if (roadLayerRef.current) { roadLayerRef.current.addTo(mapRef.current); return; }
    try {
      const { data: meta } = await supabase.from("drive_files")
        .select("parts").eq("name", "hastighet").maybeSingle();
      if (!meta) {
        throw new Error("ingen hastighetsfil i molnet än – kör Uppdatera allt under Datafiler");
      }
      const chunks = [];
      for (let p = 0; p < meta.parts; p++) {
        setStatus(`hämtar vägdata … del ${p + 1}/${meta.parts}`);
        chunks.push(await downloadData(`HASTIGHET.PART${String(p).padStart(2, "0")}`));
      }
      const total = chunks.reduce((a, c) => a + c.byteLength, 0);
      const whole = new Uint8Array(total);
      let off = 0;
      for (const c of chunks) { whole.set(new Uint8Array(c), off); off += c.byteLength; }
      const points = parseHastighetBin(whole.buffer);
      if (!points) throw new Error("hastighetsfilen gick inte att läsa");
      roadLayerRef.current = createRoadLayer(points, limitColor).addTo(mapRef.current);
      setStatus(`${points.n.toLocaleString("sv-SE")} vägpunkter i lagret`);
    } catch (e) {
      setStatus(`fel: ${e.message}`);
      setShowRoads(false);
    }
  };

  // Ett spar in i lagret, i det lage som galler. Returnerar de utvidgade
  // granserna sa att anroparen kan zooma nar den vill.
  const drawTrack = (layer, { trip, pts }, m, bounds) => {
    {
      const latlngs = pts.map((p) => [p.lat, p.lon]);
      const tip =
        `Resa ${trip.trip_no} · ${fmtKm(trip.distance_m)} km · ` +
        `${purposeLabel(trip.purpose)} · ${fmtDate(trip.start_utc)}`;

      // Glow: tre lager under varandra - en bred halo i sparets egen farg, en
      // vit kant, och sist den skarpa linjen. Det ar sa kartografer lyfter en
      // linje ur en brokig karta; en puls hade dragit blicken hela tiden.
      const glow = (color, weight = 3.5) => {
        L.polyline(latlngs, { color, weight: weight + 8, opacity: 0.18 })
          .addTo(layer);
        L.polyline(latlngs, { color: "#ffffff", weight: weight + 3.5, opacity: 0.85 })
          .addTo(layer);
      };

      if (m === "varme") {
        const line = L.polyline(latlngs, {
          color: MAP_HEAT, weight: 7, opacity: 0.07,
        });
        line.addTo(layer);
        bounds = bounds ? bounds.extend(line.getBounds()) : line.getBounds();
      } else if (m === "fart") {
        // Kant och halo laggs for hela sparet i ett svep; bara den fargade
        // linjen byter farg per fartklass ovanpa.
        glow(MAP_COLOR.omarkt, 4);
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
        const color = MAP_COLOR[trip.purpose] ?? MAP_COLOR.omarkt;
        glow(color);
        const line = L.polyline(latlngs, {
          color, weight: 3.5, opacity: 0.95,
        }).bindTooltip(tip);
        line.addTo(layer);
        bounds = bounds ? bounds.extend(line.getBounds()) : line.getBounds();
      }
    }
    return bounds;
  };

  const draw = (m) => {
    const layer = layerRef.current;
    const tracks = tracksRef.current;
    if (!layer || !tracks?.length) return;
    layer.clearLayers();
    let bounds = null;
    for (const entry of tracks) bounds = drawTrack(layer, entry, m, bounds);
    if (bounds) mapRef.current.fitBounds(bounds, { padding: [24, 24] });
  };

  useEffect(() => { draw(mode); }, [mode]);

  return (
    <div className="card">
      <h2>Karta</h2>
      <div className="filters">
        <label className="laytoggle">
          <input type="checkbox" checked={showGpx}
            onChange={(e) => toggleGpx(e.target.checked)} /> Körda spår
        </label>
        <label className="laytoggle">
          <input type="checkbox" checked={showCams}
            onChange={(e) => toggleCams(e.target.checked)} /> Fartkameror
        </label>
        <label className="laytoggle">
          <input type="checkbox" checked={showRoads}
            onChange={(e) => toggleRoads(e.target.checked)} /> Vägar med hastighetsgräns
        </label>
      </div>
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
      {showRoads && (
        <div className="legend">
          {LIMIT_BINS.map((b) => (
            <span key={b.label}>
              <span className="sw" style={{ background: b.color }} />
              {b.label} km/h
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
