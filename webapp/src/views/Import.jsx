// Importen: slapp filerna fran enhetens wifi-sida har. RESOR.JSONL blir rader i
// journalen, gpx-filerna hamnar i lagringshinken och kopplas till sina resor.
//
// Redan importerade resor rors inte - annars skulle en omimport skriva over
// syften och kunder som redigerats har i appen. Enhetens fil ar sanningen om
// vad som hande; appen ar sanningen om vad det betydde.
import { useRef, useState } from "react";
import { supabase, DEVICE_ID, GPX_BUCKET } from "../lib/supabase.js";
import { tripFromJsonl } from "../lib/gpx.js";

export default function Import({ onImported }) {
  const [log, setLog] = useState([]);
  const [busy, setBusy] = useState(false);
  const [over, setOver] = useState(false);
  const inputRef = useRef(null);

  const say = (line) => setLog((xs) => [...xs, line]);

  const handleFiles = async (files) => {
    setBusy(true);
    setLog([]);
    try {
      for (const f of files) {
        const name = f.name.toUpperCase();
        if (name.endsWith(".JSONL")) {
          await importJsonl(await f.text());
        } else if (name.endsWith(".GPX")) {
          await importGpx(name, f);
        } else if (name.endsWith(".CSV")) {
          say(`${f.name}: csv-dagboken behövs inte – släpp RESOR.JSONL i stället`);
        } else {
          say(`${f.name}: okänd filtyp, hoppar över`);
        }
      }
      onImported?.();
    } finally {
      setBusy(false);
    }
  };

  const importJsonl = async (text) => {
    const lines = text.split("\n").filter((l) => l.trim());
    const parsed = lines.map((l) => tripFromJsonl(l, DEVICE_ID)).filter(Boolean);
    say(`RESOR.JSONL: ${parsed.length} resor i filen`);
    if (!parsed.length) return;

    const { data: existing, error } = await supabase
      .from("drive_trips").select("trip_no").eq("device_id", DEVICE_ID);
    if (error) { say(`fel: ${error.message}`); return; }

    const have = new Set((existing ?? []).map((r) => r.trip_no));
    const fresh = parsed.filter((r) => !have.has(r.trip_no));
    if (!fresh.length) { say("alla fanns redan – ingenting ändrat"); return; }

    const { error: e2 } = await supabase.from("drive_trips").insert(fresh);
    if (e2) { say(`fel: ${e2.message}`); return; }
    say(`${fresh.length} nya resor importerade`);
  };

  const importGpx = async (name, file) => {
    const path = `${DEVICE_ID}/${name}`;
    const { error } = await supabase.storage
      .from(GPX_BUCKET).upload(path, file, {
        upsert: true, contentType: "application/gpx+xml",
      });
    if (error) { say(`${name}: ${error.message}`); return; }

    // Koppla sparet till sin resa via filnamnet R0042.GPX -> resa 42.
    const m = name.match(/^R(\d+)\.GPX$/);
    if (m) {
      await supabase.from("drive_trips")
        .update({ gpx_path: path })
        .eq("device_id", DEVICE_ID).eq("trip_no", parseInt(m[1], 10));
    }
    say(`${name}: uppladdad${m ? ` och kopplad till resa ${parseInt(m[1], 10)}` : ""}`);
  };

  return (
    <div className="card">
      <h2>Importera från enheten</h2>
      <p style={{ color: "var(--dim)", marginTop: 0 }}>
        Anslut telefonen till bilens wifi (<b>DriveLogger</b>), hämta
        <b> RESOR.JSONL</b> och GPX-filerna från sidan som öppnas, och släpp
        dem här. Redan importerade resor rörs inte, så dina redigeringar är
        säkra.
      </p>
      <div
        className={`drop${over ? " over" : ""}`}
        onDragOver={(e) => { e.preventDefault(); setOver(true); }}
        onDragLeave={() => setOver(false)}
        onDrop={(e) => {
          e.preventDefault(); setOver(false);
          handleFiles([...e.dataTransfer.files]);
        }}
        onClick={() => inputRef.current?.click()}
        role="button"
      >
        {busy ? "arbetar …" : "Släpp RESOR.JSONL och GPX-filer här, eller klicka för att välja"}
        <input ref={inputRef} type="file" multiple hidden
          accept=".jsonl,.gpx,.csv,application/gpx+xml"
          onChange={(e) => handleFiles([...e.target.files])} />
      </div>
      <div style={{ marginTop: "1rem" }}>
        {log.map((l, i) => <p key={i} className="status">{l}</p>)}
      </div>
    </div>
  );
}
