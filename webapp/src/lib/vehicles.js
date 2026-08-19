// Flottan: bilarna och deras milersattning.
//
// Ersattningen ar en egenskap hos bilen, for Skatteverkets schablon beror pa
// vad det ar for bil: egen bil, formansbil pa bensin/diesel/etanol, eller
// formansbil pa el. Beloppen ar 2026 ars (oforandrade sedan 2024); den som
// vill nagot annat valjer eget belopp pa bilen.

export const RATE_KINDS = [
  { value: "egen", label: "Egen bil – SKV 25 kr/mil", rate: 25 },
  { value: "formansbil", label: "Förmånsbil bensin/diesel – SKV 12 kr/mil", rate: 12 },
  { value: "formansbil_el", label: "Förmånsbil el – SKV 9,50 kr/mil", rate: 9.5 },
  { value: "egen_belopp", label: "Eget belopp", rate: null },
];

export function vehicleRate(v) {
  if (!v) return 25;
  if (v.rate_kind === "egen_belopp") return v.rate_custom ?? 25;
  return RATE_KINDS.find((k) => k.value === v.rate_kind)?.rate ?? 25;
}

export function rateLabel(kind) {
  return RATE_KINDS.find((k) => k.value === kind)?.label ?? kind;
}

export function vehicleLabel(v) {
  if (!v) return "–";
  return v.regnr && v.regnr !== v.name ? `${v.name} (${v.regnr})` : v.name;
}

// Resor fran tiden fore flottan saknar bil och raknas till den forsta - det
// ar dit de blev flyttade i databasen, och dit nya hamnar om enheten annu
// inte kopplats till nagon bil.
export function tripVehicleId(trip, vehicles) {
  return trip.vehicle_id ?? vehicles[0]?.id ?? null;
}
