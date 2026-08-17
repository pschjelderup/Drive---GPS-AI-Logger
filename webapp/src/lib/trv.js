// Trafikverkets oppna data, hamtad i webblasaren och packad till enhetens
// binarformat. Samma logik som tools/hamta-trafikverket.py, oversatt - och
// samma hardvunna laxa: pagineringens slut ar en TOM sida, inte en ofull.
// En changeid-sida kan innehalla farre poster an begart fast mer data aterstar;
// den som stannar dar far med sig en brakdel av landet och marker det inte.

const API = "https://api.trafikinfo.trafikverket.se/v2/data.json";

async function query(key, xmlQuery) {
  const res = await fetch(API, {
    method: "POST",
    headers: { "Content-Type": "text/xml" },
    body: `<REQUEST><LOGIN authenticationkey="${key}"/>${xmlQuery}</REQUEST>`,
  });
  if (!res.ok) throw new Error(`Trafikverket svarade ${res.status}`);
  const payload = await res.json();
  const result = payload?.RESPONSE?.RESULT?.[0];
  if (result?.ERROR) throw new Error(result.ERROR.MESSAGE ?? "okänt API-fel");
  return result;
}

// "POINT (lon lat)" -> [lat, lon]
function parsePoint(wkt) {
  const m = /POINT\s*Z?\s*\(\s*(-?[\d.]+)\s+(-?[\d.]+)/.exec(wkt ?? "");
  if (!m) return null;
  const lon = parseFloat(m[1]);
  const lat = parseFloat(m[2]);
  if (lat < 54 || lat > 70 || lon < 10 || lon > 25) return null;
  return [lat, lon];
}

// "LINESTRING Z (lon lat z, ...)" -> [[lat, lon], ...]
function parseLine(wkt) {
  const m = /LINESTRING\s*Z?\s*\((.*)\)/i.exec(wkt ?? "");
  if (!m) return [];
  const out = [];
  for (const part of m[1].split(",")) {
    const nums = part.trim().split(/\s+/);
    if (nums.length >= 2) {
      const lon = parseFloat(nums[0]);
      const lat = parseFloat(nums[1]);
      if (Number.isFinite(lat) && Number.isFinite(lon)) out.push([lat, lon]);
    }
  }
  return out;
}

// Punkter var femtionde meter langs linjen. Enheten soker narmaste punkt inom
// sextio meter, sa femtio haller tackningen med marginal.
function* densify(coords, stepM = 50) {
  if (!coords.length) return;
  yield coords[0];
  let carry = 0;
  for (let i = 1; i < coords.length; i++) {
    const [lat1, lon1] = coords[i - 1];
    const [lat2, lon2] = coords[i];
    const mcos = Math.cos(((lat1 + lat2) / 2) * Math.PI / 180);
    const dx = (lon2 - lon1) * 111320 * mcos;
    const dy = (lat2 - lat1) * 110540;
    const seg = Math.hypot(dx, dy);
    if (seg <= 0) continue;
    let t = (stepM - carry) / seg;
    while (t <= 1) {
      yield [lat1 + (lat2 - lat1) * t, lon1 + (lon2 - lon1) * t];
      t += stepM / seg;
    }
    carry = (carry + seg) % stepM;
  }
  yield coords[coords.length - 1];
}

// ---- kamerorna: sma nog att hamtas pa en gang
export async function fetchCameras(key, onProgress) {
  onProgress?.("hämtar kameror …");
  const result = await query(
    key,
    `<QUERY objecttype="TrafficSafetyCamera" schemaversion="1" limit="20000"/>`,
  );
  const rows = result?.TrafficSafetyCamera ?? [];
  const cams = [];
  for (const r of rows) {
    if (r.Deleted === true) continue;
    const p = parsePoint(r.Geometry?.WGS84);
    if (!p) continue;
    const bearing = Number.isFinite(r.Bearing)
      ? ((Math.round(r.Bearing) % 360) + 360) % 360
      : 0xffff;
    cams.push({ lat: p[0], lon: p[1], bearing });
  }
  onProgress?.(`${cams.length} kameror hämtade`);
  return cams;
}

// ---- hastighetsgranserna: pagineras med changeid tills en TOM sida kommer
export async function fetchLimits(key, onProgress) {
  const CHUNK = 2_000_000;
  let lat = new Int32Array(CHUNK);
  let lon = new Int32Array(CHUNK);
  let lim = new Uint8Array(CHUNK);
  let n = 0;

  const push = (la, lo, li) => {
    if (n === lat.length) {
      const nl = new Int32Array(lat.length * 2); nl.set(lat); lat = nl;
      const no = new Int32Array(lon.length * 2); no.set(lon); lon = no;
      const ni = new Uint8Array(lim.length * 2); ni.set(lim); lim = ni;
    }
    lat[n] = Math.round(la * 1e7);
    lon[n] = Math.round(lo * 1e7);
    lim[n] = li;
    n++;
  };

  let change = "0";
  let prev = null;
  let page = 0;
  const limit = 20000;

  for (;;) {
    page++;
    const result = await query(
      key,
      `<QUERY objecttype="Hastighetsgräns" namespace="vägdata.nvdb_dk_o" ` +
      `schemaversion="1.2" changeid="${change}" limit="${limit}">` +
      `<INCLUDE>Högsta_tillåtna_hastighet</INCLUDE>` +
      `<INCLUDE>Geometry.WKT-WGS84-3D</INCLUDE>` +
      `<INCLUDE>Deleted</INCLUDE><INCLUDE>Valid_To</INCLUDE></QUERY>`,
    );
    const rows = result?.["Hastighetsgräns"] ?? [];
    change = result?.INFO?.LASTCHANGEID ?? "";

    for (const r of rows) {
      if (r.Deleted === true) continue;
      if (String(r.Valid_To ?? "9999") < "2026") continue;
      const v = parseInt(r["Högsta_tillåtna_hastighet"], 10);
      if (!Number.isFinite(v) || v < 5 || v > 130) continue;
      const coords = parseLine(r.Geometry?.["WKT-WGS84-3D"]);
      for (const [la, lo] of densify(coords)) {
        if (la >= 54 && la <= 70 && lo >= 10 && lo <= 25) push(la, lo, v);
      }
    }

    onProgress?.(`sida ${page}: ${rows.length} sträckor, ${n.toLocaleString("sv-SE")} punkter`);
    if (rows.length === 0 || !change || change === prev) break;
    prev = change;
  }

  return { lat: lat.subarray(0, n), lon: lon.subarray(0, n), lim: lim.subarray(0, n), n };
}

// ---- sortering, stadning och packning till enhetens format

function sortIndex(lat, lon, n) {
  const idx = new Uint32Array(n);
  for (let i = 0; i < n; i++) idx[i] = i;
  // Flyttalsnyckel: lat dominerar, lon skiljer grannar. Precisionen racker
  // gott for en sorteringsordning.
  const key = new Float64Array(n);
  for (let i = 0; i < n; i++) key[i] = lat[i] * 4e9 + lon[i];
  return idx.sort((a, b) => key[a] - key[b]);
}

export function buildHastighetBin(points, onProgress) {
  const { lat, lon, lim, n } = points;
  onProgress?.("sorterar …");
  const idx = sortIndex(lat, lon, n);

  onProgress?.("städar dubbletter …");
  const keep = [];
  let pl = -(2 ** 31), po = 0, pv = -1;
  for (const i of idx) {
    if (lim[i] === pv && Math.abs(lat[i] - pl) < 30 && Math.abs(lon[i] - po) < 60) continue;
    keep.push(i);
    pl = lat[i]; po = lon[i]; pv = lim[i];
  }

  const buf = new ArrayBuffer(12 + keep.length * 10);
  const dv = new DataView(buf);
  dv.setUint32(0, 0x314c4844, true); // "DLH1"
  dv.setUint16(4, 1, true);
  dv.setUint16(6, 10, true);
  dv.setUint32(8, keep.length, true);
  let off = 12;
  for (const i of keep) {
    dv.setInt32(off, lat[i], true);
    dv.setInt32(off + 4, lon[i], true);
    dv.setUint8(off + 8, lim[i]);
    dv.setUint8(off + 9, 0);
    off += 10;
  }
  onProgress?.(`${keep.length.toLocaleString("sv-SE")} punkter, ${(buf.byteLength / 1048576).toFixed(1)} MB`);
  return buf;
}

// Kamerafilen far vagens skyltade hastighet inbakad ur samma punktmoln.
export function buildKamerorBin(cams, points, onProgress) {
  onProgress?.("bakar in hastigheter i kamerorna …");
  const { lat, lon, lim, n } = points ?? { n: 0 };

  // Rutnat pa en hundradels grad, ungefar en kilometer.
  const grid = new Map();
  for (let i = 0; i < n; i++) {
    const k = `${Math.floor(lat[i] / 1e5)}:${Math.floor(lon[i] / 1e5)}`;
    let arr = grid.get(k);
    if (!arr) grid.set(k, (arr = []));
    arr.push(i);
  }

  const nearest = (qlat, qlon) => {
    const cla = Math.floor((qlat * 1e7) / 1e5);
    const clo = Math.floor((qlon * 1e7) / 1e5);
    const mcos = Math.cos((qlat * Math.PI) / 180);
    let best = 0, bd = 120;
    for (let dy = -1; dy <= 1; dy++) {
      for (let dx = -1; dx <= 1; dx++) {
        for (const i of grid.get(`${cla + dy}:${clo + dx}`) ?? []) {
          const d = Math.hypot(
            (lat[i] / 1e7 - qlat) * 110540,
            (lon[i] / 1e7 - qlon) * 111320 * mcos,
          );
          if (d < bd) { bd = d; best = lim[i]; }
        }
      }
    }
    return best;
  };

  const rows = cams
    .map((c) => ({
      lat: Math.round(c.lat * 1e7),
      lon: Math.round(c.lon * 1e7),
      bearing: c.bearing,
      limit: n ? nearest(c.lat, c.lon) : 0,
    }))
    .sort((a, b) => a.lat - b.lat || a.lon - b.lon);

  const buf = new ArrayBuffer(12 + rows.length * 12);
  const dv = new DataView(buf);
  dv.setUint32(0, 0x31434c44, true); // "DLC1"
  dv.setUint16(4, 1, true);
  dv.setUint16(6, 12, true);
  dv.setUint32(8, rows.length, true);
  let off = 12;
  let withLimit = 0;
  for (const r of rows) {
    dv.setInt32(off, r.lat, true);
    dv.setInt32(off + 4, r.lon, true);
    dv.setUint16(off + 8, r.bearing, true);
    dv.setUint8(off + 10, r.limit);
    dv.setUint8(off + 11, 0);
    if (r.limit) withLimit++;
    off += 12;
  }
  onProgress?.(`${rows.length} kameror, ${withLimit} med skyltsiffra`);
  return buf;
}

export async function sha8(buf) {
  const d = await crypto.subtle.digest("SHA-256", buf);
  return Array.from(new Uint8Array(d)).slice(0, 8)
    .map((b) => b.toString(16).padStart(2, "0")).join("");
}
