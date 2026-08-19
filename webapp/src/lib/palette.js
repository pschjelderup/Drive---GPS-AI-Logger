// Diagram- och kartfargerna.
//
// Diagramfargerna ar CSS-variabler, sa att de foljer temat: ljusa och morka
// varianterna ar var for sig korda genom dataviz-valideringen (ljushetsband,
// kromagolv, CVD-separation, normalsyn, kontrast) pa sin respektive yta.
// Diagrammen satter dem via style-attributet - presentation-attribut i SVG
// kan inte lasa var(), style kan.

export const PURPOSE_COLOR = {
  privat: "var(--p-privat)",
  foretag: "var(--p-foretag)",
  diffust: "var(--p-diffust)",
  omarkt: "var(--p-omarkt)",
};

// En ensam serie ar bla. Status ar reserverade och aterkommer aldrig som serie.
export const SERIES = "var(--series)";
export const STATUS_WARNING = "var(--warn)";
export const STATUS_CRITICAL = "var(--red)";

// Ytor och black, gemensamma for alla diagram.
export const SURFACE = "var(--panel)";
export const INK = "var(--chart-ink)";
export const INK_SECONDARY = "var(--dim)";
export const INK_MUTED = "var(--chart-ink-muted)";
export const GRID = "var(--chart-grid)";
export const BASELINE = "var(--chart-baseline)";

// Kartans farger ar bokstavliga - Leaflet satter dem som SVG-attribut dar
// var() inte fungerar - och valda mot OpenStreetMaps egen gronska: djupbla,
// magenta och brand orange finns inte i kartbilden och separerar dessutom
// under fargblindhet (validerade mot OSM-beige yta, alla PASS). Den grona
// syftesfargen fran diagrammen far alltsa inte folja med hit - det var den
// som smalt in i skogen.
export const MAP_COLOR = {
  privat: "#1d4ed8",
  foretag: "#c2187e",
  diffust: "#d95f02",
  omarkt: "#57534e",
};

// Varmekartan: magenta lyser pa bade skog, stad och sno.
export const MAP_HEAT = "#c026d3";

// Fartlagret: absolut fart i fem steg, gron till rod via gult - en ramp, inte
// kategorier, sa har far ljusheten bara falla monotont.
export const SPEED_BINS = [
  { max: 30, color: "#1a7f37", label: "under 30" },
  { max: 60, color: "#7fb069", label: "30-60" },
  { max: 90, color: "#eda100", label: "60-90" },
  { max: 110, color: "#e2571b", label: "90-110" },
  { max: Infinity, color: "#c2187e", label: "över 110" },
];

export function speedColor(kmh) {
  for (const b of SPEED_BINS) {
    if (kmh < b.max) return b.color;
  }
  return SPEED_BINS[SPEED_BINS.length - 1].color;
}
