// Tre lagen: ljust, morkt, folj systemet. Valet ligger i localStorage och
// tillampas fore forsta malningen av snutten i index.html - har hanteras bara
// byten medan sidan ar oppen.
import { useEffect, useState } from "react";

// Nya nyckeln sedan namnbytet; den gamla lases som reserv sa att ett redan
// gjort temaval overlever. Skrivningar gar alltid till den nya.
const KEY = "hikaya-theme";
const OLD_KEY = "drivelogger-theme";

function apply(mode) {
  const dark = mode === "dark" ||
    (mode === "system" &&
      window.matchMedia("(prefers-color-scheme: dark)").matches);
  document.documentElement.dataset.theme = dark ? "dark" : "light";
}

const stored = () =>
  localStorage.getItem(KEY) ?? localStorage.getItem(OLD_KEY) ?? "system";

export default function ThemeToggle() {
  const [mode, setMode] = useState(stored);

  useEffect(() => {
    const mq = window.matchMedia("(prefers-color-scheme: dark)");
    const onChange = () => {
      if (stored() === "system") apply("system");
    };
    mq.addEventListener("change", onChange);
    return () => mq.removeEventListener("change", onChange);
  }, []);

  const pick = (m) => {
    localStorage.setItem(KEY, m);
    setMode(m);
    apply(m);
  };

  const options = [
    { value: "light", glyph: "☀", label: "Ljust tema" },
    { value: "dark", glyph: "☾", label: "Mörkt tema" },
    { value: "system", glyph: "⚙", label: "Följ systemet" },
  ];

  return (
    <span className="themetoggle" role="radiogroup" aria-label="Tema">
      {options.map((o) => (
        <button key={o.value} role="radio" aria-checked={mode === o.value}
          aria-label={o.label} title={o.label}
          className={mode === o.value ? "active" : ""}
          onClick={() => pick(o.value)}>{o.glyph}</button>
      ))}
    </span>
  );
}
