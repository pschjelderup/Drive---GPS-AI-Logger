// Installningarna: flottan, enheten och kundlistan. API-nycklarna bor i
// Vercels miljovariabler och har inget att gora har - de ska inte ligga i
// nagon webblasare.
import { useEffect, useState } from "react";
import { supabase } from "../lib/supabase.js";
import { fmtDateTime } from "../lib/fmt.js";
import { RATE_KINDS, vehicleLabel } from "../lib/vehicles.js";

// Kundens kontorsposition: soks upp via Places pa bolagsnamnet och gar att
// finjustera for hand. Positionen ar det som later journalen kanna igen ett
// kundbesok i en resas start- eller malpunkt.
function CustomerPos({ customer, onSave }) {
  const [open, setOpen] = useState(false);
  const [q, setQ] = useState(customer.name);
  const [options, setOptions] = useState([]);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState("");

  const search = async (text) => {
    setBusy(true); setErr("");
    try {
      const { data: { session } } = await supabase.auth.getSession();
      const res = await fetch(`/api/places?q=${encodeURIComponent(text)}`, {
        headers: { Authorization: `Bearer ${session?.access_token ?? ""}` },
      });
      const body = await res.json();
      if (!res.ok) throw new Error(body.error ?? `svar ${res.status}`);
      setOptions((body.places ?? []).filter((p) => p.lat != null));
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
        {customer.lat != null
          ? `${customer.lat.toFixed(4)}, ${customer.lon.toFixed(4)} `
          : ""}
        <button className="ghost mini"
          onClick={() => { setOpen(true); setQ(customer.name); search(customer.name); }}>
          {customer.lat != null ? "ändra" : "position …"}
        </button>
      </span>
    );
  }

  return (
    <div className="placepop">
      <input autoFocus type="text" value={q}
        onChange={(e) => setQ(e.target.value)}
        onKeyDown={(e) => {
          if (e.key === "Enter") search(q.trim());
          if (e.key === "Escape") setOpen(false);
        }} />
      <div className="placelist">
        {busy && <span className="status">söker …</span>}
        {err && <span className="status error">{err}</span>}
        {!busy && options.map((p, i) => (
          <button key={i} type="button"
            onClick={() => { onSave(p.lat, p.lon); setOpen(false); }}>
            {p.name} · {p.address}
          </button>
        ))}
        {customer.lat != null && (
          <button type="button" className="danger"
            onClick={() => { onSave(null, null); setOpen(false); }}>
            rensa positionen
          </button>
        )}
        <button type="button" onClick={() => setOpen(false)}>stäng</button>
      </div>
      <span className="status">eller skriv själv:</span>
      <div style={{ display: "flex", gap: ".3rem" }}>
        <input type="text" placeholder="lat" style={{ width: "6.5rem" }}
          defaultValue={customer.lat ?? ""} id={`clat${customer.id}`} />
        <input type="text" placeholder="lon" style={{ width: "6.5rem" }}
          defaultValue={customer.lon ?? ""} id={`clon${customer.id}`} />
        <button className="ghost mini" onClick={() => {
          const la = parseFloat(document.getElementById(`clat${customer.id}`).value.replace(",", "."));
          const lo = parseFloat(document.getElementById(`clon${customer.id}`).value.replace(",", "."));
          if (Number.isFinite(la) && Number.isFinite(lo)) {
            onSave(la, lo);
            setOpen(false);
          }
        }}>spara</button>
      </div>
    </div>
  );
}

// Faktureringen: det allmanna priset per mil ut till kund. Kundens eget pris
// (i kundlistan) vinner alltid over det har.
function BillingCard() {
  const [rate, setRate] = useState("");
  const [status, setStatus] = useState("");

  useEffect(() => {
    supabase.from("drive_settings").select("*")
      .eq("key", "debiterat_per_mil").maybeSingle()
      .then(({ data }) => {
        const v = data?.value;
        if (v != null) setRate(String(v).replace(".", ","));
      });
  }, []);

  const save = async () => {
    const n = parseFloat(rate.replace(",", "."));
    const { error } = await supabase.from("drive_settings").upsert({
      key: "debiterat_per_mil",
      value: Number.isFinite(n) ? n : null,
      updated_at: new Date().toISOString(),
    });
    setStatus(error ? error.message : "sparat");
  };

  return (
    <div className="card">
      <h2>Fakturering</h2>
      <p style={{ color: "var(--dim)", marginTop: 0 }}>
        Debiterat pris per mil ut till kund. Kunder med eget pris i
        kundlistan använder det i stället – det här är reservvärdet, och det
        som gäller för företagsresor utan kund.
      </p>
      <div style={{ display: "flex", gap: ".5rem", alignItems: "center" }}>
        <input type="text" inputMode="decimal" placeholder="kr/mil"
          value={rate} style={{ width: "7rem" }}
          onChange={(e) => setRate(e.target.value)} />
        <button className="primary" onClick={save}>Spara</button>
        <span className="status">{status}</span>
      </div>
    </div>
  );
}

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
      <p style={{ marginBottom: 0 }}>
        <a href="https://pschjelderup.github.io/Drive---GPS-AI-Logger/"
          target="_blank" rel="noreferrer" style={{ fontWeight: 600 }}>
          Flasha enheten med senaste firmware →
        </a>{" "}
        <span className="status">
          öppnas i Chrome/Edge med enheten i USB-porten
        </span>
      </p>
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

  const patchCustomer = async (id, fields) => {
    setCustomers((xs) => xs.map((c) => (c.id === id ? { ...c, ...fields } : c)));
    const { error } = await supabase
      .from("drive_customers").update(fields).eq("id", id);
    if (error) setStatus(error.message);
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
      <BillingCard />
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
        <div style={{ overflowX: "auto" }}>
        <table className="journal">
          <thead>
            <tr><th>Kund</th><th>Kr/mil</th><th>Kontorets position</th><th></th></tr>
          </thead>
          <tbody>
            {customers.map((c) => (
              <tr key={c.id}>
                <td style={{ opacity: c.active ? 1 : 0.4 }}>{c.name}</td>
                <td>
                  <input type="text" inputMode="decimal" placeholder="kr/mil"
                    style={{ width: "5.5rem" }}
                    defaultValue={c.rate_per_mil ?? ""}
                    onBlur={(e) => {
                      const n = parseFloat(e.target.value.replace(",", "."));
                      patchCustomer(c.id, {
                        rate_per_mil: Number.isFinite(n) ? n : null,
                      });
                    }} />
                </td>
                <td>
                  <CustomerPos customer={c}
                    onSave={(lat, lon) => patchCustomer(c.id, { lat, lon })} />
                </td>
                <td style={{ textAlign: "right" }}>
                  <button className="ghost" onClick={() => toggle(c)}>
                    {c.active ? "dölj" : "visa igen"}
                  </button>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
        </div>
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
