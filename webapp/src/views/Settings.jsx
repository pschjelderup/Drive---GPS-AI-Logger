// Installningarna: AI-nyckeln, kundlistan och exporten till enheten.
import { useEffect, useState } from "react";
import { supabase } from "../lib/supabase.js";
import { fmtDateTime } from "../lib/fmt.js";
import { AI_KEY_STORAGE } from "./Ai.jsx";

function DeviceCard() {
  const [devices, setDevices] = useState([]);
  const [shown, setShown] = useState(false);

  useEffect(() => {
    supabase.from("drive_devices").select("*").order("id")
      .then(({ data }) => setDevices(data ?? []));
  }, []);

  return (
    <div className="card">
      <h2>Enheten och molnsynken</h2>
      <p style={{ color: "var(--dim)", marginTop: 0 }}>
        Skriv in token nedan på enhetens wifi-sida under <b>Molnsynk</b>,
        tillsammans med din iPhone-hotspots namn och lösenord. Sedan laddar
        enheten upp resor och hämtar datafiler själv, varje gång den har wifi
        och ingen resa pågår.
      </p>
      {devices.map((d) => (
        <div key={d.id} style={{ marginBottom: ".6rem" }}>
          <b>{d.name ?? d.id}</b>{" "}
          <span className="status">
            senast sedd {d.last_seen ? fmtDateTime(d.last_seen) : "aldrig"} ·
            synkad t.o.m. resa {d.last_synced_trip}
          </span>
          <div style={{ display: "flex", gap: ".5rem", marginTop: ".3rem" }}>
            <code style={{
              background: "#0d131d", padding: ".35rem .6rem",
              borderRadius: "8px", fontSize: ".85rem",
            }}>
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
  const [key, setKey] = useState(localStorage.getItem(AI_KEY_STORAGE) ?? "");
  const [customers, setCustomers] = useState([]);
  const [newName, setNewName] = useState("");
  const [status, setStatus] = useState("");

  const load = async () => {
    const { data } = await supabase
      .from("drive_customers").select("*").order("name");
    setCustomers(data ?? []);
  };
  useEffect(() => { load(); }, []);

  const saveKey = () => {
    const v = key.trim();
    if (v) {
      localStorage.setItem(AI_KEY_STORAGE, v);
      setStatus("nyckeln sparad i den här webbläsaren");
    } else {
      localStorage.removeItem(AI_KEY_STORAGE);
      setStatus("nyckeln borttagen");
    }
  };

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
      <DeviceCard />
      <div className="card">
        <h2>Anthropic-nyckel för AI-analysen</h2>
        <p style={{ color: "var(--dim)", marginTop: 0 }}>
          Skapas på console.anthropic.com. Nyckeln sparas bara i den här
          webbläsarens localStorage – aldrig i databasen – och används enbart
          för anrop direkt till Anthropic.
        </p>
        <div style={{ display: "flex", gap: ".5rem", flexWrap: "wrap" }}>
          <input type="password" placeholder="sk-ant-..." value={key}
            onChange={(e) => setKey(e.target.value)}
            style={{ flex: 1, minWidth: "16rem" }} />
          <button className="primary" onClick={saveKey}>Spara</button>
        </div>
        <p className="status">{status}</p>
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
