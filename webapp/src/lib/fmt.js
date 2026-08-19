// Formattering pa svenska. Datum och tider visas i Europe/Stockholm oavsett
// var lasaren rakar vara - resorna kordes i Sverige.

export const kmFmt = new Intl.NumberFormat("sv-SE", {
  maximumFractionDigits: 1,
  minimumFractionDigits: 1,
});

export const intFmt = new Intl.NumberFormat("sv-SE");

export function fmtKm(m) {
  return kmFmt.format((m || 0) / 1000);
}

export function fmtDateTime(iso) {
  if (!iso) return "–";
  return new Date(iso).toLocaleString("sv-SE", {
    timeZone: "Europe/Stockholm",
    dateStyle: "short",
    timeStyle: "short",
  });
}

export function fmtDate(iso) {
  if (!iso) return "–";
  return new Date(iso).toLocaleDateString("sv-SE", {
    timeZone: "Europe/Stockholm",
    day: "numeric",
    month: "short",
  });
}

// Filstorlekar med ratt enhet. Kamerafilen ar 33 kB - avrundad till "0.0 MB"
// ser den trasig ut fast den ar precis som den ska.
export function fmtBytes(b) {
  if (b == null) return "–";
  if (b < 1024) return `${b} B`;
  if (b < 1048576) return `${Math.round(b / 1024)} kB`;
  return `${(b / 1048576).toFixed(1)} MB`;
}

export function fmtDur(s) {
  if (!s) return "0 min";
  const h = Math.floor(s / 3600);
  const m = Math.round((s % 3600) / 60);
  return h > 0 ? `${h} tim ${m} min` : `${m} min`;
}

export const PURPOSES = [
  { value: "privat", label: "Privat" },
  { value: "foretag", label: "Företag" },
  { value: "diffust", label: "Diffust" },
];

export function purposeLabel(p) {
  return PURPOSES.find((x) => x.value === p)?.label ?? p ?? "Omärkt";
}
