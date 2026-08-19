// Datafilerna: hamta kameror och hastighetsgranser fran Trafikverket, bygg
// enhetens binarfiler, och lagg dem i molnhinken - dar enheten hamtar dem
// sjalv nasta gang den har wifi. Hastighetsfilen laggs delad i bitar under
// lagringstjanstens filstorleksgrans; enheten syr ihop dem vid nedladdning.
import { useEffect, useState } from "react";
import { supabase } from "../lib/supabase.js";
import {
  fetchCameras, fetchLimits, buildHastighetBin, buildKamerorBin,
  parseHastighetBin, sha8,
} from "../lib/trv.js";
import { fmtDateTime } from "../lib/fmt.js";

const TRV_KEY_STORAGE = "drivelogger_trv_key";
const PART_BYTES = 40 * 1024 * 1024;

export default function DataFiles() {
  const [trvKey, setTrvKey] = useState(
    localStorage.getItem(TRV_KEY_STORAGE) ?? "",
  );
  const [files, setFiles] = useState([]);
  const [log, setLog] = useState([]);
  const [busy, setBusy] = useState(false);

  const say = (line) =>
    setLog((xs) => [...xs.slice(-14), line]);

  const loadMeta = async () => {
    const { data } = await supabase.from("drive_files").select("*");
    setFiles(data ?? []);
  };
  useEffect(() => { loadMeta(); }, []);

  const saveKey = () => {
    localStorage.setItem(TRV_KEY_STORAGE, trvKey.trim());
    say("Trafikverket-nyckeln sparad i den här webbläsaren");
  };

  const uploadParts = async (name, buf) => {
    const parts = Math.ceil(buf.byteLength / PART_BYTES);
    for (let p = 0; p < parts; p++) {
      const slice = buf.slice(p * PART_BYTES, (p + 1) * PART_BYTES);
      const key = parts === 1 && name === "KAMEROR"
        ? "KAMEROR.BIN"
        : `${name}.PART${String(p).padStart(2, "0")}`;
      say(`laddar upp ${key} (${(slice.byteLength / 1048576).toFixed(1)} MB) …`);
      const { error } = await supabase.storage.from("drive-data")
        .upload(key, slice, {
          upsert: true, contentType: "application/octet-stream",
        });
      if (error) throw new Error(`${key}: ${error.message}`);
    }
    return parts;
  };

  const registerFile = async (name, buf, parts) => {
    const version = await sha8(buf);
    const { error } = await supabase.from("drive_files").upsert({
      name, version, size_bytes: buf.byteLength, parts,
      updated_at: new Date().toISOString(),
    });
    if (error) throw new Error(error.message);
    say(`${name}: version ${version} registrerad – enheten hämtar vid nästa synk`);
    loadMeta();
  };

  // Skyltsiffrorna till kameraknappen kommer ur molnets redan uppladdade
  // hastighetsfil, inte ur en ny hamtning fran Trafikverket - det ar det som
  // gor knappen snabb utan att den bakar blint. En kamerafil utan siffror har
  // en gang skrivit over en bra fil; det misstaget gors inte om.
  const downloadPoints = async () => {
    const meta = files.find((f) => f.name === "hastighet");
    if (!meta) return null;
    const chunks = [];
    for (let p = 0; p < meta.parts; p++) {
      const key = `HASTIGHET.PART${String(p).padStart(2, "0")}`;
      say(`hämtar ${key} ur molnet …`);
      const { data, error } = await supabase.storage.from("drive-data").download(key);
      if (error || !data) return null;
      chunks.push(await data.arrayBuffer());
    }
    const total = chunks.reduce((a, b) => a + b.byteLength, 0);
    const whole = new Uint8Array(total);
    let off = 0;
    for (const c of chunks) { whole.set(new Uint8Array(c), off); off += c.byteLength; }
    return parseHastighetBin(whole.buffer);
  };

  const updateCameras = async () => {
    const key = trvKey.trim();
    if (!key) { say("lägg in Trafikverket-nyckeln först"); return; }
    setBusy(true);
    try {
      const cams = await fetchCameras(key, say);
      const points = await downloadPoints();
      if (!points) {
        say("ingen hastighetsfil i molnet än – kamerorna får inga skyltsiffror. Kör \"Uppdatera allt\" för att få med dem.");
      }
      const buf = buildKamerorBin(cams, points, say);
      await uploadParts("KAMEROR", buf);
      await registerFile("kameror", buf, 1);
    } catch (e) {
      say(`fel: ${e.message}`);
    } finally {
      setBusy(false);
    }
  };

  const updateAll = async () => {
    const key = trvKey.trim();
    if (!key) { say("lägg in Trafikverket-nyckeln först"); return; }
    setBusy(true);
    try {
      say("Hela Sverige tar en stund – låt fliken vara öppen.");
      const points = await fetchLimits(key, say);
      const hast = buildHastighetBin(points, say);
      const hParts = await uploadParts("HASTIGHET", hast);
      await registerFile("hastighet", hast, hParts);

      const cams = await fetchCameras(key, say);
      const kam = buildKamerorBin(cams, points, say);
      await uploadParts("KAMEROR", kam);
      await registerFile("kameror", kam, 1);
    } catch (e) {
      say(`fel: ${e.message}`);
    } finally {
      setBusy(false);
    }
  };

  return (
    <>
      <div className="card">
        <h2>Trafikverket-nyckel</h2>
        <p style={{ color: "var(--dim)", marginTop: 0 }}>
          Gratis på data.trafikverket.se. Sparas bara i den här webbläsaren.
        </p>
        <div style={{ display: "flex", gap: ".5rem", flexWrap: "wrap" }}>
          <input type="password" placeholder="api-nyckel" value={trvKey}
            onChange={(e) => setTrvKey(e.target.value)}
            style={{ flex: 1, minWidth: "16rem" }} />
          <button className="primary" onClick={saveKey}>Spara</button>
        </div>
      </div>

      <div className="card">
        <h2>Datafiler i molnet</h2>
        <table className="journal">
          <thead>
            <tr><th>Fil</th><th>Version</th><th>Storlek</th><th>Uppdaterad</th></tr>
          </thead>
          <tbody>
            {files.map((f) => (
              <tr key={f.name}>
                <td>{f.name}</td>
                <td><code>{f.version}</code></td>
                <td>{(f.size_bytes / 1048576).toFixed(1)} MB
                  {f.parts > 1 ? ` (${f.parts} delar)` : ""}</td>
                <td>{fmtDateTime(f.updated_at)}</td>
              </tr>
            ))}
            {!files.length && (
              <tr><td colSpan="4" className="status">inga filer uppladdade än</td></tr>
            )}
          </tbody>
        </table>
        <div style={{ display: "flex", gap: ".6rem", marginTop: "1rem", flexWrap: "wrap" }}>
          <button className="primary" onClick={updateAll} disabled={busy}>
            {busy ? "arbetar …" : "Uppdatera allt (kameror + hastigheter)"}
          </button>
          <button className="ghost" onClick={updateCameras} disabled={busy}>
            Bara kamerorna (snabbt)
          </button>
        </div>
        <p className="status" style={{ marginTop: ".6rem" }}>
          Hastigheterna är hela NVDB – över två miljoner sträckor – och tar
          några minuter att hämta. Kör helst på en dator. Enheten laddar sedan
          ner filerna själv nästa gång den har wifi och ingen resa pågår.
        </p>
      </div>

      <div className="card">
        <h2>Förlopp</h2>
        <div className="ai-out" style={{ minHeight: "2rem" }}>
          {log.length ? log.join("\n") : "–"}
        </div>
      </div>
    </>
  );
}
