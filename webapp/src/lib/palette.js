// Diagramfargerna. Syftestrion ar korda genom dataviz-valideringen pa mork yta
// (ljushetsband, kromagolv, CVD-separation, normalsyn, kontrast - alla PASS),
// sa den fargblinde ser skillnad pa staplarna och inte bara pa etiketterna.
// UI-ytorna behaller enhetens HUD-farger; det har ar seriefargerna i diagram.

export const PURPOSE_COLOR = {
  privat: "#3987e5",
  foretag: "#199e70",
  diffust: "#c98500",
  omarkt: "#898781",
};

// En ensam serie ar bla. Status ar reserverade och aterkommer aldrig som serie.
export const SERIES = "#3987e5";
export const STATUS_WARNING = "#fab219";
export const STATUS_CRITICAL = "#d03b3b";

// Ytor och black, gemensamma for alla diagram.
export const SURFACE = "#141a24";
export const INK = "#e6edf3";
export const INK_SECONDARY = "#c3c2b7";
export const INK_MUTED = "#898781";
export const GRID = "#2c2c2a";
export const BASELINE = "#383835";
