// Toyota-kopplingen under Inställningar.
//
// Tre lägen: ingen koppling (inloggningsformulär), inloggad men bilen inte
// vald (Toyotas bilar att koppla), och kopplade bilar med bilens läge och
// synken. Lösenordet går direkt till molnfunktionen och sparas aldrig – det
// som sparas är Toyotas token, i en tabell webbläsaren inte kan läsa.
import { useEffect, useState } from "react";
import { supabase } from "../lib/supabase.js";
import { fmtDateTime } from "../lib/fmt.js";
import { vehicleLabel } from "../lib/vehicles.js";
import { toyotaCall, syncToyota } from "../lib/toyota.js";

const HISTORY = [
  { months: 3, label: "3 månader" },
  { months: 6, label: "6 månader" },
  { months: 12, label: "12 månader" },
  { months: 24, label: "2 år" },
];

function monthsAgo(n) {
  const d = new Date();
  d.setMonth(d.getMonth() - n);
  return d.toISOString().slice(0, 10);
}

// VIN visas bara med sina sista sex tecken – det räcker för att känna igen
// bilen och är ingenting att sprida vidare.
function shortVin(vin) {
  return vin ? `…${vin.slice(-6)}` : "";
}

function LoginForm({ onDone, username: preset = "", note }) {
  const [username, setUsername] = useState(preset);
  const [password, setPassword] = useState("");
  const [brand, setBrand] = useState("T");
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState(note ?? "");

  const submit = async (e) => {
    e.preventDefault();
    if (!username.trim() || !password) { setStatus("e-post och lösenord behövs"); return; }
    setBusy(true);
    setStatus("loggar in hos Toyota …");
    try {
      const r = await toyotaCall({ action: "connect", username, password, brand });
      setPassword("");
      setStatus("");
      onDone(r);
    } catch (err) {
      setStatus(err.message);
    }
    setBusy(false);
  };

  return (
    <form onSubmit={submit}
      style={{ display: "flex", gap: ".5rem", flexWrap: "wrap", alignItems: "center" }}>
      <input type="email" placeholder="e-post till Toyota-kontot" value={username}
        autoComplete="off" onChange={(e) => setUsername(e.target.value)} />
      <input type="password" placeholder="lösenord" value={password}
        autoComplete="off" onChange={(e) => setPassword(e.target.value)} />
      <select value={brand} onChange={(e) => setBrand(e.target.value)}>
        <option value="T">Toyota</option>
        <option value="L">Lexus</option>
      </select>
      <button className="primary" disabled={busy}>Logga in</button>
      {status && <p className="status" style={{ flexBasis: "100%", margin: 0 }}>{status}</p>}
    </form>
  );
}

function PickVehicles({ account, vehicles, linkedVins, fleet, onLinked }) {
  const [choice, setChoice] = useState({});
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState("");

  const link = async (car) => {
    const pick = choice[car.vin] ?? "ny";
    setBusy(true);
    setStatus(`kopplar ${car.nickname ?? car.model ?? car.vin} …`);
    try {
      const r = await toyotaCall({
        action: "link",
        account_id: account,
        vin: car.vin,
        vehicle_id: pick === "ny" || pick === "" ? null : Number(pick),
        new_vehicle: pick === "ny",
      });
      setStatus("kopplad – hämtar de senaste 60 dagarnas resor …");
      const n = await syncToyota(r.device_id, {
        onProgress: (k) => setStatus(`kopplad – ${k} resor hämtade hittills …`),
      });
      setStatus(`kopplad – ${n} resor hämtade. Äldre historik hämtar du nedan.`);
      onLinked(n);
    } catch (err) {
      setStatus(err.message);
    }
    setBusy(false);
  };

  return (
    <div>
      {vehicles.map((car) => (
        <div key={car.vin} style={{ marginBottom: ".6rem" }}>
          <b>{car.nickname || car.model || "Toyota"}</b>{" "}
          <span className="status">
            {[car.model, car.year, car.regnr, shortVin(car.vin)].filter(Boolean).join(" · ")}
          </span>
          {linkedVins.has(car.vin) ? (
            <span className="status"> · redan kopplad</span>
          ) : (
            <div style={{ display: "flex", gap: ".5rem", marginTop: ".3rem", flexWrap: "wrap", alignItems: "center" }}>
              <label style={{ fontSize: ".85rem", color: "var(--dim)" }}>
                resorna förs på{" "}
                <select value={choice[car.vin] ?? "ny"}
                  onChange={(e) => setChoice((c) => ({ ...c, [car.vin]: e.target.value }))}>
                  <option value="ny">en ny bil i flottan</option>
                  {fleet.map((v) => (
                    <option key={v.id} value={v.id}>{vehicleLabel(v)}</option>
                  ))}
                  <option value="">ingen bil</option>
                </select>
              </label>
              <button className="primary" disabled={busy} onClick={() => link(car)}>Koppla</button>
            </div>
          )}
        </div>
      ))}
      {status && <p className="status">{status}</p>}
    </div>
  );
}

function LinkedCar({ d, fleet, devices, onChanged }) {
  const c = d.connector ?? {};
  const [status, setStatus] = useState("");
  const [busy, setBusy] = useState(false);
  const [months, setMonths] = useState(12);
  const [relogin, setRelogin] = useState(false);

  // En Hikaya-enhet i samma bil ger samma resor två gånger. Det sägs rakt ut.
  const twin = d.vehicle_id && devices.find((x) =>
    x.kind !== "toyota" && x.vehicle_id === d.vehicle_id);

  const run = async (opts, label) => {
    setBusy(true);
    setStatus(`${label} …`);
    try {
      const n = await syncToyota(d.id, {
        ...opts,
        onProgress: (k) => setStatus(`${label} – ${k} nya resor hittills …`),
      });
      setStatus(n ? `${n} nya resor` : "inga nya resor");
      onChanged(n);
    } catch (err) {
      setStatus(err.message);
      if (err.needsLogin) setRelogin(true);
      onChanged(0);
    }
    setBusy(false);
  };

  const setVehicle = async (vehicleId) => {
    const vehicle_id = vehicleId ? Number(vehicleId) : null;
    await supabase.from("drive_devices").update({ vehicle_id }).eq("id", d.id);
    onChanged(0);
  };

  const unlink = async () => {
    if (!window.confirm(
      `Koppla bort ${d.name ?? d.id}? Redan hämtade resor ligger kvar i journalen. ` +
      "Är det sista bilen på Toyota-kontot raderas även inloggningen.",
    )) return;
    try {
      await toyotaCall({ action: "unlink", device_id: d.id });
      onChanged(0);
    } catch (err) {
      setStatus(err.message);
    }
  };

  const facts = [
    c.odometer_km != null && `mätare ${Math.round(c.odometer_km).toLocaleString("sv-SE")} km`,
    c.fuel_pct != null && `tank ${c.fuel_pct} %`,
    c.battery_pct != null && `batteri ${c.battery_pct} %`,
    c.range_km != null && `räckvidd ${c.range_km} km`,
  ].filter(Boolean);

  return (
    <div style={{ marginBottom: "1rem" }}>
      <b>{d.name ?? d.id}</b>{" "}
      <span className="status">
        {[c.model, c.year, c.regnr, shortVin(c.vin)].filter(Boolean).join(" · ")}
      </span>
      <div className="status" style={{ marginTop: ".2rem" }}>
        {facts.length ? facts.join(" · ") : "bilens läge hämtas vid nästa synk"}
        {c.lat != null && (
          <> · <a href={`https://www.google.com/maps?q=${c.lat},${c.lon}`}
            target="_blank" rel="noreferrer">står här</a>
            {c.located_at ? ` (${fmtDateTime(c.located_at)})` : ""}</>
        )}
      </div>
      <div className="status">
        {c.last_sync ? `synkad ${fmtDateTime(c.last_sync)}` : "aldrig synkad"}
        {c.username ? ` · konto ${c.username}` : ""}
      </div>
      {c.last_error && (
        <p className="status error" style={{ margin: ".2rem 0" }}>{c.last_error}</p>
      )}
      {twin && (
        <p className="status" style={{ margin: ".2rem 0" }}>
          Obs: {twin.name ?? twin.id} sitter i samma bil – resorna kommer då med två
          gånger. Koppla loss enheten från bilen, eller Toyota-kopplingen.
        </p>
      )}
      {(relogin || c.needs_login) && (
        <div style={{ margin: ".4rem 0" }}>
          <LoginForm username={c.username ?? ""}
            note="Toyota vill ha lösenordet igen – inloggningen hade gått ut."
            onDone={() => { setRelogin(false); run({}, "synkar"); }} />
        </div>
      )}
      <div style={{ display: "flex", gap: ".5rem", marginTop: ".4rem", flexWrap: "wrap", alignItems: "center" }}>
        <label style={{ fontSize: ".85rem", color: "var(--dim)" }}>
          resorna förs på{" "}
          <select value={d.vehicle_id ?? ""} onChange={(e) => setVehicle(e.target.value)}>
            <option value="">– ingen bil –</option>
            {fleet.map((v) => (
              <option key={v.id} value={v.id}>{vehicleLabel(v)}</option>
            ))}
          </select>
        </label>
        <button className="primary" disabled={busy} onClick={() => run({}, "synkar")}>
          Synka nu
        </button>
        <select value={months} onChange={(e) => setMonths(Number(e.target.value))}>
          {HISTORY.map((h) => <option key={h.months} value={h.months}>{h.label}</option>)}
        </select>
        <button className="ghost" disabled={busy}
          onClick={() => run({ from: monthsAgo(months), to: new Date().toISOString().slice(0, 10) },
            "hämtar historik")}>
          Hämta historik
        </button>
        <button className="ghost" onClick={unlink}>koppla bort</button>
      </div>
      {status && <p className="status" style={{ margin: ".3rem 0 0" }}>{status}</p>}
    </div>
  );
}

export default function ToyotaCard({ onTripsChanged }) {
  const [devices, setDevices] = useState([]);
  const [fleet, setFleet] = useState([]);
  const [account, setAccount] = useState(null); // {account_id, vehicles}
  const [adding, setAdding] = useState(false);

  const load = async () => {
    const [d, v] = await Promise.all([
      supabase.from("drive_devices").select("*").order("id"),
      supabase.from("drive_vehicles").select("*").eq("active", true).order("id"),
    ]);
    setDevices(d.data ?? []);
    setFleet(v.data ?? []);
  };
  useEffect(() => { load(); }, []);

  const toyotas = devices.filter((d) => d.kind === "toyota");
  const linkedVins = new Set(toyotas.map((d) => d.connector?.vin).filter(Boolean));

  const changed = (n) => {
    load();
    if (n > 0) onTripsChanged?.();
  };

  return (
    <div className="card">
      <h2>Toyota-koppling</h2>
      <p style={{ color: "var(--dim)", marginTop: 0 }}>
        En Toyota eller Lexus med Toyota Connected Services lämnar själv sina
        resor – start, mål, sträcka, tid, förbrukning, körpoäng och hela rutten –
        till Toyota. Här hämtas de in i journalen som resor från bilen, utan
        någon enhet i bilen. Mätarställningen följer med som avstämning.
        Lösenordet används bara till inloggningen och sparas aldrig; det som
        sparas är Toyotas inloggningstoken, oåtkomlig för webbläsaren.
      </p>
      <p className="status" style={{ marginTop: 0 }}>
        Kopplingen använder samma inofficiella väg som MyToyota-appen och
        biblioteket pytoyoda. Toyota kan ändra den utan förvarning.
      </p>

      {toyotas.map((d) => (
        <LinkedCar key={d.id} d={d} fleet={fleet} devices={devices} onChanged={changed} />
      ))}

      {account ? (
        <>
          <PickVehicles account={account.account_id} vehicles={account.vehicles}
            linkedVins={linkedVins} fleet={fleet}
            onLinked={(n) => { changed(n); }} />
          <button className="ghost" onClick={() => { setAccount(null); setAdding(false); }}>
            klar
          </button>
        </>
      ) : (toyotas.length === 0 || adding) ? (
        <LoginForm onDone={(r) => setAccount(r)} />
      ) : (
        <button className="ghost" onClick={() => setAdding(true)}>
          Koppla en bil till
        </button>
      )}
    </div>
  );
}
