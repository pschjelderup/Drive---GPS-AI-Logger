// Supabase-klienten. Nyckeln ar den publicerbara - den ar gjord for att ligga
// i klientkod, och all atkomst styrs av RLS: inloggad ser drive_-tabellerna,
// anonym ser ingenting.
import { createClient } from "@supabase/supabase-js";

export const supabase = createClient(
  "https://jdjkeloiwjkcycelmexq.supabase.co",
  "sb_publishable_d3O3Vk2vwNNV8piYGEcffA_kuyZdKCW",
);

export const DEVICE_ID = "drivelogger-1";
export const GPX_BUCKET = "drive-gpx";
