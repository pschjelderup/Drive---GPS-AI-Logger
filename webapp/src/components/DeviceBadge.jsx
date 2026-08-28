// Enheternas puls i sidhuvudet: gron och pulserande nar en enhet varit
// uppkopplad de senaste tio minuterna, annars nar den senast horde av sig.
// En bricka per enhet - med en enhet per bil ser man har vilka bilar som
// pratar med molnet just nu. Enheten rapporterar in sig vid varje molnanrop
// (last_seen), sa "uppkopplad" betyder typiskt att bilen star pa hotspoten
// och synkar.
import { useEffect, useState } from "react";
import { supabase } from "../lib/supabase.js";
import { fmtDateTime } from "../lib/fmt.js";

const ONLINE_MS = 10 * 60 * 1000;

export default function DeviceBadge() {
  const [devices, setDevices] = useState([]);
  // Tickar sa att en enhet som tystnat glider over fran "uppkopplad" till
  // tidsstampeln aven om ingen ny hamtning skett.
  const [, setTick] = useState(0);

  useEffect(() => {
    let alive = true;
    const load = async () => {
      const { data } = await supabase
        .from("drive_devices").select("id, name, last_seen, last_synced_trip")
        .order("id");
      if (alive) setDevices(data ?? []);
    };
    load();
    const t = setInterval(() => { load(); setTick((n) => n + 1); }, 60000);
    return () => { alive = false; clearInterval(t); };
  }, []);

  if (!devices.length) return null;
  const many = devices.length > 1;

  return (
    <>
      {devices.map((d) => {
        const seen = d.last_seen ? new Date(d.last_seen).getTime() : 0;
        const online = seen && Date.now() - seen < ONLINE_MS;
        // Med flera enheter ar namnet det som skiljer brickorna at; med en
        // racker "enheten", precis som forut.
        const who = many ? (d.name ?? d.id) : "enheten";
        return (
          <span key={d.id} className={online ? "badge online" : "badge"}
            title={`Synkad t.o.m. resa ${d.last_synced_trip}`}>
            <span className="dot" />
            {online
              ? `${who} uppkopplad`
              : seen
                ? `${who} synkade ${fmtDateTime(d.last_seen)}`
                : `${who} har aldrig synkat`}
          </span>
        );
      })}
    </>
  );
}
