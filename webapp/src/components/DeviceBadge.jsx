// Enhetens puls i sidhuvudet: gron och pulserande nar den varit uppkopplad de
// senaste tio minuterna, annars nar den senast horde av sig, med datum och
// klockslag. Enheten rapporterar in sig vid varje molnanrop (last_seen), sa
// "uppkopplad" betyder att den pratar med molnet just nu - typiskt att bilen
// star hemma pa hotspoten och synkar.
import { useEffect, useState } from "react";
import { supabase } from "../lib/supabase.js";
import { fmtDateTime } from "../lib/fmt.js";

const ONLINE_MS = 10 * 60 * 1000;

export default function DeviceBadge() {
  const [device, setDevice] = useState(null);
  // Tickar sa att en enhet som tystnat glider over fran "uppkopplad" till
  // tidsstampeln aven om ingen ny hamtning skett.
  const [, setTick] = useState(0);

  useEffect(() => {
    let alive = true;
    const load = async () => {
      const { data } = await supabase
        .from("drive_devices").select("id, name, last_seen, last_synced_trip")
        .order("id").limit(1).maybeSingle();
      if (alive) setDevice(data ?? null);
    };
    load();
    const t = setInterval(() => { load(); setTick((n) => n + 1); }, 60000);
    return () => { alive = false; clearInterval(t); };
  }, []);

  if (!device) return null;

  const seen = device.last_seen ? new Date(device.last_seen).getTime() : 0;
  const online = seen && Date.now() - seen < ONLINE_MS;

  return (
    <span className={online ? "badge online" : "badge"}
      title={`Synkad t.o.m. resa ${device.last_synced_trip}`}>
      <span className="dot" />
      {online
        ? "enheten uppkopplad"
        : seen
          ? `enheten synkade ${fmtDateTime(device.last_seen)}`
          : "enheten har aldrig synkat"}
    </span>
  );
}
