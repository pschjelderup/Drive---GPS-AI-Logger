// Hikaya-appen: inloggning, flikar och vyerna. Samma formsprak som
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
      <h1><Logo className="logo" /> Hikaya</h1>
      <p className="byline">by Creative Enabler</p>
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
  // Flikarna lever kvar nar man lamnar dem. Forr avmonterades vyn vid varje
  // byte, och Korjournal gjorde da om sina sex fragor och hela
  // luckfyllnadspasset varje gang man kom tillbaka. Nu monteras en flik
  // forsta gangen den oppnas och goms sedan bara - kartan, journalen och
  // rapporten ar dar man lamnade dem, pa en gang.
  const [visited, setVisited] = useState(() => new Set(["journal"]));
  const open = (key) => {
    setTab(key);
    setVisited((v) => (v.has(key) ? v : new Set(v).add(key)));
  };
  const view = (key, node) =>
    visited.has(key) ? <div hidden={tab !== key}>{node}</div> : null;

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
        <h1>Hikaya <span className="byline">by Creative Enabler</span></h1>
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
            onClick={() => open(t.key)}>{t.label}</button>
        ))}
      </nav>
      {view("journal", <Journal key={epoch} />)}
      {view("rapport", <Report />)}
      {view("karta", <MapView visible={tab === "karta"} />)}
      {view("fart", <Speeding />)}
      {view("eco", <Eco />)}
      {view("ai", <Ai />)}
      {view("import", <Import onImported={() => setEpoch((e) => e + 1)} />)}
      {view("data", <DataFiles />)}
      {view("install", <Settings />)}
    </div>
  );
}
