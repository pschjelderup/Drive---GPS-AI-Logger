// Installningarna: flottan, enheten och kundlistan. API-nycklarna bor i
// Vercels miljovariabler och har inget att gora har - de ska inte ligga i
// nagon webblasare.
import { useEffect, useState } from "react";
import { supabase } from "../lib/supabase.js";
import { fmtDateTime } from "../lib/fmt.js";
import { RATE_KINDS, vehicleLabel } from "../lib/vehicles.js";

// Flottan: bilarna med namn, regnummer och sin ersattningstyp. Ersattningen
// ar en egenskap hos bilen - Skatteverkets schablon beror pa om det ar egen
// bil eller formansbil - och rapporten hamtar sitt belopp harifran.
function FleetCard() {
  const [vehicles, setVehicles] = useState([]);
  const [newName, setNewName] = useState("");
  const [newRegnr, setNewRegnr] = useState("");
  const [status, setStatus] = useState("");

  const load = async () => {
    const { data } = await supabase
      .from("drive_vehicles").select("*").order("id");
    setVehicles(data ?? []);
  };
  useEffect(() => { load(); }, []);

  const patch = async (id, fields) => {
    setVehicles((xs) => xs.map((v) => (v.id === id ? { ...v, ...fields } : v)));
    const { error } = await supabase
      .from("drive_vehicles").update(fields).eq("id", id);
    if (error) setStatus(error.message);
  };

  const add = async () => {
    const regnr = newRegnr.trim().toUpperCase() || null;
    const name = newName.trim() || regnr;
    if (!name) { setStatus("skriv namn eller regnummer"); return; }
    const { error } = await supabase
      .from("drive_vehicles").insert({ name, regnr });
    if (error) { setStatus(error.message); return; }
    setNewName(""); setNewRegnr(""); setStatus("");
    load();
  };

  return (
    <div className="card">
      <h2>Flottan</h2>
      <div style={{ overflowX: "auto" }}>
        <table className="journal">
          <thead>
            <tr><th>Namn</th><th>Regnr</th><th>Milersättning</th><th></th></tr>
          </thead>
          <tbody>
            {vehicles.map((v) => (
              <tr key={v.id} style={{ opacity: v.active ? 1 : 0.45 }}>
                <td>
                  <input type="text" defaultValue={v.name}
                    style={{ width: "9rem" }}
                    onBlur={(e) => {
                      const name = e.target.value.trim();
                      if (name && name !== v.name) patch(v.id, { name });
                    }} />
                </td>
                <td>
                  <input type="text" defaultValue={v.regnr ?? ""}
                    style={{ width: "6.5rem" }}
                    onBlur={(e) => patch(v.id, {
                      regnr: e.target.value.trim().toUpperCase() || null,
                    })} />
                </td>
                <td>
                  <div style={{ display: "flex", gap: ".4rem", alignItems: "center" }}>
                    <select value={v.rate_kind}
                      onChange={(e) => patch(v.id, { rate_kind: e.target.value })}>
                      {RATE_KINDS.map((k) => (
                        <option key={k.value} value={k.value}>{k.label}</option>
                      ))}
                    </select>
                    {v.rate_kind === "egen_belopp" && (
                      <input type="text" inputMode="decimal"
                        defaultValue={v.rate_custom ?? ""}
                        placeholder="kr/mil" style={{ width: "5rem" }}
                        onBlur={(e) => {
                          const n = parseFloat(e.target.value.replace(",", "."));
                          patch(v.id, { rate_custom: Number.isFinite(n) ? n : null });
                        }} />
                    )}
                  </div>
                </td>
                <td style={{ textAlign: "right" }}>
                  <button className="ghost" onClick={() => patch(v.id, { active: !v.active })}>
                    {v.active ? "dölj" : "visa igen"}
                  </button>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      <div style={{ display: "flex", gap: ".5rem", marginTop: ".8rem", flexWrap: "wrap" }}>
        <input type="text" placeholder="namn (t.ex. Tjänstebilen)" value={newName}
          onChange={(e) => setNewName(e.target.value)} />
        <input type="text" placeholder="regnr" value={newRegnr}
          style={{ width: "7rem" }}
          onChange={(e) => setNewRegnr(e.target.value)}
          onKeyDown={(e) => e.key === "Enter" && add()} />
        <button className="primary" onClick={add}>Lägg till bil</button>
      </div>
      <p className="status">{status || "Skatteverkets schabloner 2026: 25 kr/mil egen bil · 12 kr/mil förmånsbil · 9,50 kr/mil förmånsbil el."}</p>
    </div>
  );
}

function DeviceCard() {
  const [devices, setDevices] = useState([]);
  const [vehicles, setVehicles] = useState([]);
  const [shown, setShown] = useState(false);

  useEffect(() => {
    supabase.from("drive_devices").select("*").order("id")
      .then(({ data }) => setDevices(data ?? []));
    supabase.from("drive_vehicles").select("*").eq("active", true).order("id")
      .then(({ data }) => setVehicles(data ?? []));
  }, []);

  const setVehicle = async (d, vehicleId) => {
    const vehicle_id = vehicleId ? Number(vehicleId) : null;
    setDevices((xs) => xs.map((x) => (x.id === d.id ? { ...x, vehicle_id } : x)));
    await supabase.from("drive_devices").update({ vehicle_id }).eq("id", d.id);
  };

  return (
    <div className="card">
      <h2>Enheten och molnsynken</h2>
      <p style={{ color: "var(--dim)", marginTop: 0 }}>
        Skriv in token nedan på enhetens wifi-sida under <b>Molnsynk</b>,
        tillsammans med din iPhone-hotspots namn och lösenord. Sedan laddar
        enheten upp resor och hämtar datafiler själv, varje gång den har wifi
        och ingen resa pågår. Resorna bokförs på den bil enheten sitter i.
      </p>
      {devices.map((d) => (
        <div key={d.id} style={{ marginBottom: ".6rem" }}>
          <b>{d.name ?? d.id}</b>{" "}
          <span className="status">
            senast sedd {d.last_seen ? fmtDateTime(d.last_seen) : "aldrig"} ·
            synkad t.o.m. resa {d.last_synced_trip}
          </span>
          <div style={{ display: "flex", gap: ".5rem", marginTop: ".3rem", flexWrap: "wrap", alignItems: "center" }}>
            <label style={{ fontSize: ".85rem", color: "var(--dim)" }}>
              sitter i{" "}
              <select value={d.vehicle_id ?? ""}
                onChange={(e) => setVehicle(d, e.target.value)}>
                <option value="">– ingen bil –</option>
                {vehicles.map((v) => (
                  <option key={v.id} value={v.id}>{vehicleLabel(v)}</option>
                ))}
              </select>
            </label>
            <code className="token">
              {shown ? d.token : "••••••••••••••••"}
            </code>
            <button className="ghost" onClick={() => setShown((v) => !v)}>
              {shown ? "dölj" : "visa"}
            </button>
          </div>
        </div>
      ))}
    </div>
  );
}

export default function Settings() {
  const [customers, setCustomers] = useState([]);
  const [newName, setNewName] = useState("");
  const [status, setStatus] = useState("");

  const load = async () => {
    const { data } = await supabase
      .from("drive_customers").select("*").order("name");
    setCustomers(data ?? []);
  };
  useEffect(() => { load(); }, []);

  const addCustomer = async () => {
    const name = newName.trim();
    if (!name) return;
    const { error } = await supabase.from("drive_customers").insert({ name });
    if (error) { setStatus(error.message); return; }
    setNewName("");
    load();
  };

  const toggle = async (c) => {
    await supabase.from("drive_customers")
      .update({ active: !c.active }).eq("id", c.id);
    load();
  };

  // KUNDER.CSV i exakt det format enheten laser: ett namn per rad.
  const exportKunder = () => {
    const rows = customers.filter((c) => c.active).map((c) => c.name);
    const blob = new Blob([rows.join("\n") + "\n"], {
      type: "text/csv;charset=utf-8",
    });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "KUNDER.CSV";
    a.click();
    URL.revokeObjectURL(a.href);
  };

  return (
    <>
      <FleetCard />
      <DeviceCard />

      <div className="card">
        <h2>API-nycklar</h2>
        <p style={{ color: "var(--dim)", margin: 0 }}>
          Trafikverket- och Anthropic-nycklarna ligger i Vercels
          miljövariabler (<code>TRAFIKVERKET_API_KEY</code> och{" "}
          <code>ANTHROPIC_API_KEY</code>, projektet drivelogger) och lämnar
          aldrig servern. Ingen nyckel sparas i webbläsaren.
        </p>
      </div>

      <div className="card">
        <h2>Kundlistan</h2>
        <table className="journal">
          <tbody>
            {customers.map((c) => (
              <tr key={c.id}>
                <td style={{ opacity: c.active ? 1 : 0.4 }}>{c.name}</td>
                <td style={{ textAlign: "right" }}>
                  <button className="ghost" onClick={() => toggle(c)}>
                    {c.active ? "dölj" : "visa igen"}
                  </button>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
        <div style={{ display: "flex", gap: ".5rem", marginTop: ".8rem" }}>
          <input type="text" placeholder="ny kund" value={newName}
            onChange={(e) => setNewName(e.target.value)}
            onKeyDown={(e) => e.key === "Enter" && addCustomer()} />
          <button className="primary" onClick={addCustomer}>Lägg till</button>
        </div>
        <p style={{ marginTop: ".8rem" }}>
          <button className="ghost" onClick={exportKunder}>
            Exportera KUNDER.CSV till enheten
          </button>
          <span className="status" style={{ marginLeft: ".8rem" }}>
            laddas upp via enhetens wifi-sida
          </span>
        </p>
      </div>
    </>
  );
}
