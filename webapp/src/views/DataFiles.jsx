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
import { fmtDateTime, fmtBytes } from "../lib/fmt.js";

// Delstorleken ar ett kontrakt med enheten (CLOUD_PART_BYTES i config.h):
// hela delar ar giltiga aterupptagningspunkter, sa en bruten nedladdning
// kostar en del - inte hela filen. Sma delar ar darfor poangen: 4 MB tar
// nagra tiotal sekunder aven over en hotspot.
const PART_BYTES = 4 * 1024 * 1024;

export default function DataFiles() {
  const [files, setFiles] = useState([]);
  const [log, setLog] = useState([]);
  const [busy, setBusy] = useState(false);
  // Manuell SD-hantering: vad som senast lades på kortet för hand, sparat i
  // drive_settings så att bocken överlever webbläsare och datorer.
  const [sdManual, setSdManual] = useState(null);
  // Enhetens egen logg, uppsynkad i poster om några kilobyte styck.
  const [deviceLog, setDeviceLog] = useState([]);

  const say = (line) =>
    setLog((xs) => [...xs.slice(-14), line]);

  const loadMeta = async () => {
    const { data } = await supabase.from("drive_files").select("*");
    setFiles(data ?? []);
    const { data: s } = await supabase.from("drive_settings")
      .select("value").eq("key", "sd_manual").maybeSingle();
    setSdManual(s?.value ?? null);
  };
  const loadDeviceLog = async () => {
    const { data } = await supabase.from("drive_device_log")
      .select("*").order("at", { ascending: false }).limit(30);
    setDeviceLog(data ?? []);
  };
  useEffect(() => { loadMeta(); loadDeviceLog(); }, []);

  const uploadParts = async (name, buf) => {
    const parts = Math.ceil(buf.byteLength / PART_BYTES);
    for (let p = 0; p < parts; p++) {
      const slice = buf.slice(p * PART_BYTES, (p + 1) * PART_BYTES);
      const key = parts === 1 && name === "KAMEROR"
        ? "KAMEROR.BIN"
        : `${name}.PART${String(p).padStart(2, "0")}`;
      say(`laddar upp ${key} (${fmtBytes(slice.byteLength)}) …`);
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

  // Hela filen som en blob - hastigheten sys ihop ur sina delar, precis som
  // enheten gör vid nedladdning. Det som hamnar på datorn är byte för byte
  // samma fil som enheten själv skulle ha lagt på kortet.
  const fetchWhole = async (name) => {
    const meta = files.find((f) => f.name === name);
    if (!meta) throw new Error(`ingen ${name}-fil i molnet än`);
    const chunks = [];
    if (name === "kameror") {
      say("hämtar KAMEROR.BIN ur molnet …");
      const { data, error } = await supabase.storage.from("drive-data")
        .download("KAMEROR.BIN");
      if (error || !data) throw new Error("KAMEROR.BIN gick inte att hämta");
      chunks.push(await data.arrayBuffer());
    } else {
      for (let p = 0; p < meta.parts; p++) {
        const key = `HASTIGHET.PART${String(p).padStart(2, "0")}`;
        say(`hämtar ${key} (${p + 1}/${meta.parts}) …`);
        const { data, error } = await supabase.storage.from("drive-data").download(key);
        if (error || !data) throw new Error(`${key} gick inte att hämta`);
        chunks.push(await data.arrayBuffer());
      }
    }
    const total = chunks.reduce((a, b) => a + b.byteLength, 0);
    if (total !== meta.size_bytes) {
      throw new Error(`storleken stämmer inte (${total} av ${meta.size_bytes} byte) – kör "Uppdatera allt" och försök igen`);
    }
    const whole = new Uint8Array(total);
    let off = 0;
    for (const c of chunks) { whole.set(new Uint8Array(c), off); off += c.byteLength; }
    return { bytes: whole, meta };
  };

  const downloadForSd = async (name, filename) => {
    setBusy(true);
    try {
      const { bytes, meta } = await fetchWhole(name);
      const url = URL.createObjectURL(new Blob([bytes], { type: "application/octet-stream" }));
      const a = document.createElement("a");
      a.href = url;
      a.download = filename;
      a.click();
      URL.revokeObjectURL(url);
      say(`${filename} nedladdad (version ${meta.version}, ${fmtBytes(meta.size_bytes)}) – lägg den i mappen DRIVE på SD-kortet`);
    } catch (e) {
      say(`fel: ${e.message}`);
    } finally {
      setBusy(false);
    }
  };

  // Bocken sparar vilka versioner som lades på kortet. Då kan sidan säga om
  // kortet fortfarande är aktuellt – och enheten känner själv igen filerna
  // vid nästa synk (rätt storlek och signatur) och hoppar över nedladdningen.
  const setSdDone = async (checked) => {
    const value = checked
      ? {
          hastighet: files.find((f) => f.name === "hastighet")?.version ?? null,
          kameror: files.find((f) => f.name === "kameror")?.version ?? null,
          at: new Date().toISOString(),
        }
      : null;
    // Alltid upsert - aven urbockningen skrivs som null, sa att raden inte
    // behover nagon raderingspolicy.
    await supabase.from("drive_settings").upsert({
      key: "sd_manual", value, updated_at: new Date().toISOString(),
    });
    setSdManual(value);
  };

  const clearDeviceLog = async () => {
    if (!window.confirm("Rensa hela enhetsloggen?")) return;
    await supabase.from("drive_device_log").delete().gte("id", 0);
    loadDeviceLog();
  };

  const updateCameras = async () => {
    setBusy(true);
    try {
      const cams = await fetchCameras(say);
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
    setBusy(true);
    try {
      say("Hela Sverige tar en stund – låt fliken vara öppen.");
      const points = await fetchLimits(say);
      const hast = buildHastighetBin(points, say);
      const hParts = await uploadParts("HASTIGHET", hast);
      await registerFile("hastighet", hast, hParts);

      const cams = await fetchCameras(say);
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
        <h2>Datafiler i molnet</h2>
        <table className="journal">
          <thead>
            <tr><th>Fil</th><th>Version</th><th>Storlek</th><th>Uppdaterad</th></tr>
          </thead>
          <tbody>
            {files.map((f) => {
              // Delformatet ar ett kontrakt med enheten: varje del utom den
              // sista ar exakt PART_BYTES. En fil uppladdad fore kontraktet
              // har farre, storre delar - enheten vagrar dem, tyst. Da ska
              // det synas HAR, inte uppdagas i bilen.
              const gammal = f.parts > 1 &&
                f.parts !== Math.ceil(f.size_bytes / PART_BYTES);
              return (
                <tr key={f.name}>
                  <td>{f.name}</td>
                  <td><code>{f.version}</code></td>
                  <td>{fmtBytes(f.size_bytes)}
                    {f.parts > 1 ? ` (${f.parts} delar)` : ""}
                    {gammal && (
                      <div className="status error" style={{ margin: 0 }}>
                        gammalt delformat – enheten kan inte hämta den.
                        Kör "Uppdatera allt".
                      </div>
                    )}</td>
                  <td>{fmtDateTime(f.updated_at)}</td>
                </tr>
              );
            })}
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
          Trafikverket-nyckeln ligger i Vercels miljövariabler
          (TRAFIKVERKET_API_KEY), inte i webbläsaren.
          Hastigheterna är hela NVDB – över två miljoner sträckor – och tar
          några minuter att hämta. Kör helst på en dator. Enheten laddar sedan
          ner filerna själv nästa gång den har wifi och ingen resa pågår.
        </p>
      </div>

      <div className="card">
        <h2>Lägg filerna på SD-kortet för hand</h2>
        <p className="status">
          Alternativ till wifi-synken: ladda ner filerna här, flytta dem till
          enhetens SD-kort med en dator, klart. Enheten känner igen dem vid
          nästa synk (rätt storlek och signatur) och hoppar då över hela
          nedladdningen själv.
        </p>
        <ol style={{ margin: ".4rem 0 .8rem 1.2rem", lineHeight: 1.7 }}>
          <li>Ladda ner båda filerna med knapparna nedan.</li>
          <li>Stäng av enheten och ta ut SD-kortet, sätt det i datorn.</li>
          <li>Lägg filerna i mappen <code>DRIVE</code> på kortet
            (ersätt de gamla). Namnen måste vara exakt{" "}
            <code>HASTIGHET.BIN</code> och <code>KAMEROR.BIN</code>.</li>
          <li>Mata ut kortet säkert, sätt tillbaka det och starta enheten.</li>
          <li>Bocka i rutan nedan så håller sidan koll på versionen.</li>
        </ol>
        <div style={{ display: "flex", gap: ".6rem", flexWrap: "wrap" }}>
          <button className="primary" disabled={busy}
            onClick={() => downloadForSd("hastighet", "HASTIGHET.BIN")}>
            Ladda ner HASTIGHET.BIN
          </button>
          <button className="ghost" disabled={busy}
            onClick={() => downloadForSd("kameror", "KAMEROR.BIN")}>
            Ladda ner KAMEROR.BIN
          </button>
        </div>
        <label style={{ display: "flex", alignItems: "center", gap: ".5rem",
          marginTop: ".9rem", cursor: "pointer" }}>
          <input type="checkbox" checked={!!sdManual}
            onChange={(e) => setSdDone(e.target.checked)} />
          Filerna är lagda på SD-kortet
        </label>
        {sdManual && (() => {
          const hastNu = files.find((f) => f.name === "hastighet")?.version;
          const kamNu = files.find((f) => f.name === "kameror")?.version;
          const aktuell = sdManual.hastighet === hastNu && sdManual.kameror === kamNu;
          return aktuell ? (
            <p className="status" style={{ color: "var(--ok, #2e7d32)" }}>
              Kortet är aktuellt – versionerna på kortet ({fmtDateTime(sdManual.at)})
              matchar molnet. Enheten behöver inte synka ner något.
            </p>
          ) : (
            <p className="status error">
              Kortet är inaktuellt – molnet har nyare versioner än de som lades
              på kortet {fmtDateTime(sdManual.at)}. Ladda ner igen, eller låt
              enheten synka via wifi.
            </p>
          );
        })()}
      </div>

      <div className="card">
        <h2>Enhetslogg</h2>
        <p className="status">
          Enhetens egen dagbok – starter, resor, synkar och fel – uppsynkad
          vid varje molnkontakt. Nyast överst.
        </p>
        <div className="ai-out" style={{ minHeight: "2rem", maxHeight: "24rem",
          overflowY: "auto", whiteSpace: "pre-wrap" }}>
          {deviceLog.length
            ? deviceLog.map((p) =>
                `── mottaget ${fmtDateTime(p.at)} ──\n${p.content.trim()}`
              ).join("\n")
            : "ingen logg uppsynkad än – kräver firmware med enhetsloggen"}
        </div>
        <div style={{ display: "flex", gap: ".6rem", marginTop: ".6rem" }}>
          <button className="ghost" onClick={loadDeviceLog}>Uppdatera</button>
          <button className="ghost" onClick={clearDeviceLog}
            disabled={!deviceLog.length}>Rensa loggen</button>
        </div>
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
