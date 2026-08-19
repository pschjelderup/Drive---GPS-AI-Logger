// DriveLogger-appen: inloggning, flikar och vyerna. Samma formsprak som
// skarmen i bilen - papper, black och vagbla - sa att de kanns som tva sidor
// av samma sak.
import { useEffect, useState } from "react";
import { supabase } from "./lib/supabase.js";
import ThemeToggle from "./components/ThemeToggle.jsx";
import DeviceBadge from "./components/DeviceBadge.jsx";
import Journal from "./views/Journal.jsx";
import Report from "./views/Report.jsx";
import Import from "./views/Import.jsx";
import MapView from "./views/MapView.jsx";
import Speeding from "./views/Speeding.jsx";
import Eco from "./views/Eco.jsx";
import Ai from "./views/Ai.jsx";
import DataFiles from "./views/DataFiles.jsx";
import Settings from "./views/Settings.jsx";

function Logo({ className }) {
  return <img src="/brand/app-icon.svg" alt="" className={className} />;
}

function Login() {
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);

  const signIn = async (e) => {
    e.preventDefault();
    setBusy(true); setError("");
    const { error } = await supabase.auth.signInWithPassword({ email, password });
    if (error) setError(error.message);
    setBusy(false);
  };

  return (
    <form className="login" onSubmit={signIn}>
      <h1><Logo className="logo" /> DriveLogger</h1>
      <p>Körjournalen, kartan och analyserna. Logga in med ditt konto.</p>
      <input type="email" placeholder="e-post" value={email}
        autoComplete="username" onChange={(e) => setEmail(e.target.value)} />
      <input type="password" placeholder="lösenord" value={password}
        autoComplete="current-password"
        onChange={(e) => setPassword(e.target.value)} />
      <button className="primary" disabled={busy}>Logga in</button>
      {error && <p className="status error">{error}</p>}
    </form>
  );
}

const TABS = [
  { key: "journal", label: "Körjournal" },
  { key: "rapport", label: "Rapport" },
  { key: "karta", label: "Karta" },
  { key: "fart", label: "Fortkörning" },
  { key: "eco", label: "Ecodrive" },
  { key: "ai", label: "AI-analys" },
  { key: "import", label: "Importera" },
  { key: "data", label: "Datafiler" },
  { key: "install", label: "Inställningar" },
];

export default function App() {
  const [session, setSession] = useState(undefined);
  const [tab, setTab] = useState("journal");
  // Nyckel som byts efter en import, sa att journalen hamtar om sina rader.
  const [epoch, setEpoch] = useState(0);

  useEffect(() => {
    supabase.auth.getSession().then(({ data }) => setSession(data.session));
    const { data: sub } = supabase.auth.onAuthStateChange(
      (_evt, s) => setSession(s),
    );
    return () => sub.subscription.unsubscribe();
  }, []);

  if (session === undefined) return null;
  if (!session) return <Login />;

  return (
    <div className="shell">
      <header className="top">
        <Logo className="logo" />
        <h1>DriveLogger</h1>
        <DeviceBadge />
        <span className="who">
          <ThemeToggle />
          {session.user.email}{" "}
          <button className="ghost"
            onClick={() => supabase.auth.signOut()}>logga ut</button>
        </span>
      </header>
      <nav className="tabs">
        {TABS.map((t) => (
          <button key={t.key} className={tab === t.key ? "active" : ""}
            onClick={() => setTab(t.key)}>{t.label}</button>
        ))}
      </nav>
      {tab === "journal" && <Journal key={epoch} />}
      {tab === "rapport" && <Report />}
      {tab === "karta" && <MapView />}
      {tab === "fart" && <Speeding />}
      {tab === "eco" && <Eco />}
      {tab === "ai" && <Ai />}
      {tab === "import" && <Import onImported={() => setEpoch((e) => e + 1)} />}
      {tab === "data" && <DataFiles />}
      {tab === "install" && <Settings />}
    </div>
  );
}
