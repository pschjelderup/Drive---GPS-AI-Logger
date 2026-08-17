#!/usr/bin/env python3
"""Hamtar fartkameror och hastighetsgranser och skriver filerna som enheten laser.

Fartkamerorna kommer fran Trafikverkets oppna API, objekttypen
TrafficSafetyCamera. Datan ar CC0 - ingen attribution kravs, inga villkor att
halla reda pa. En gratis API-nyckel hamtas pa https://data.trafikverket.se/.

    export TRV_API_KEY=din-nyckel
    ./hamta-trafikverket.py kameror --ut KAMEROR.BIN

Hastighetsgranserna kommer fran NVDB, som sedan 2025 ligger i samma oppna API
under namespace vagdata.nvdb_dk_o - samma nyckel, inget konto, ingen Lastkajen:

    ./hamta-trafikverket.py granser --api --ut HASTIGHET.BIN

Hamtningen pagineras med changeid och tar en stund; hela Sverige ar over en
miljon strackor. Den som hellre utgar fran en nedladdad fil kan fortfarande:

    ./hamta-trafikverket.py granser --in hastighet.geojson --ut HASTIGHET.BIN

Och eftersom kameraposterna saknar hastighetsuppgift helt gar det att baka in
vagens grans i varje kamera nar filen skapas, ur samma NVDB-fil:

    ./hamta-trafikverket.py kameror --granser hastighet.geojson --ut KAMEROR.BIN

Bada filerna laggs i mappen DRIVE pa minneskortet.

Om faltnamnen: objekttypen har enligt schemat falten ID, Name, Bearing,
Geometry.WGS84, Geometry.SWEREF99TM, RoadNumber, Counties, IconId, Deleted och
ModifiedTime. Nagon skyltad hastighet finns inte - darav inbakningen ovan.
Skriptet begar alla falt och letar bland dem, sa ett schemabyte ger ett tydligt
besked i stallet for en tom fil:

    ./hamta-trafikverket.py kameror --visa-falt
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import struct
import sys
import urllib.request

API = "https://api.trafikinfo.trafikverket.se/v2/data.json"

# ---------------------------------------------------------------- filformat --
#
# Bada filerna ar sorterade pa latitud i vaxande ordning. Det ar det som gor att
# enheten kan sla upp en position med binarsokning i stallet for att jamfora mot
# varje rad - en bil pa vag 73 behover inte fraga om kameror i Kiruna.
#
# Rubrik, 12 byte:
#   char[4]  magi          "DLC1" for kameror, "DLH1" for granser
#   uint16   version       1
#   uint16   poststorlek   12 respektive 10
#   uint32   antal
#
# Kamerapost, 12 byte:
#   int32    lat           grader * 1e7
#   int32    lon           grader * 1e7
#   uint16   riktning      0-359 grader medsols fran norr, 65535 = okand
#   uint8    hastighet     km/h, 0 = okand
#   uint8    flaggor       bit 0 = ATK-stracka (snittfart mats)
#
# Granspost, 10 byte:
#   int32    lat
#   int32    lon
#   uint8    hastighet     km/h
#   uint8    flaggor       reserverad, 0

CAM_MAGIC = b"DLC1"
LIMIT_MAGIC = b"DLH1"
BEARING_UNKNOWN = 0xFFFF
FLAG_AVERAGE_SPEED = 0x01


def write_table(path: str, magic: bytes, record_size: int, rows: list[bytes]) -> None:
    with open(path, "wb") as f:
        f.write(magic)
        f.write(struct.pack("<HHI", 1, record_size, len(rows)))
        for r in rows:
            f.write(r)


def in_sweden(lat: float, lon: float) -> bool:
    return 54.0 <= lat <= 70.0 and 10.0 <= lon <= 25.0


# ------------------------------------------------------------- trafikverket --


def query(key: str, objecttype: str, schemaversion: str, limit: int) -> list[dict]:
    """Begar alla falt objekttypen har. Utan INCLUDE far vi hela posten, och da
    behover skriptet inte veta faltnamnen i forvag."""
    body = (
        f'<REQUEST><LOGIN authenticationkey="{key}"/>'
        f'<QUERY objecttype="{objecttype}" schemaversion="{schemaversion}" '
        f'limit="{limit}"/></REQUEST>'
    )
    req = urllib.request.Request(
        API, data=body.encode("utf-8"), headers={"Content-Type": "text/xml"}
    )
    with urllib.request.urlopen(req, timeout=90) as resp:
        payload = json.load(resp)

    result = payload.get("RESPONSE", {}).get("RESULT", [])
    if not result:
        raise SystemExit("Trafikverket svarade utan RESULT. Kontrollera nyckeln.")

    first = result[0]
    if "ERROR" in first:
        raise SystemExit(f"Trafikverket: {first['ERROR']}")

    # Objekttypen ar nyckeln i svaret, oavsett vad den heter.
    for _name, rows in first.items():
        if isinstance(rows, list):
            return rows
    raise SystemExit(f"Hittade ingen lista i svaret: {list(first.keys())}")


def deep_get(row: dict, names: tuple[str, ...]):
    """Letar efter ett falt bland flera mojliga stavningar, aven ett steg ned."""
    for n in names:
        if n in row and row[n] not in (None, ""):
            return row[n]
    for value in row.values():
        if isinstance(value, dict):
            for n in names:
                if n in value and value[n] not in (None, ""):
                    return value[n]
    return None


WKT_POINT = re.compile(r"POINT\s*\(\s*(-?[\d.]+)\s+(-?[\d.]+)")


def parse_point(value) -> "tuple[float, float] | None":
    """Trafikverket levererar geometrin som WKT, "POINT (lon lat)". Ordningen ar
    lon fore lat, vilket ar tvartemot hur man sager det - darav den egna
    funktionen i stallet for en uppackning pa plats."""
    if isinstance(value, dict):
        value = deep_get(value, ("WGS84", "wgs84", "WGS84_3D"))
    if not isinstance(value, str):
        return None
    m = WKT_POINT.search(value)
    if not m:
        return None
    lon, lat = float(m.group(1)), float(m.group(2))
    if not in_sweden(lat, lon):
        return None  # utanfor Sverige - nagot ar fel, hellre hoppa over
    return lat, lon


# ------------------------------------------------------------ hastighetsdata -

LIMIT_KEYS = (
    "Högsta_tillåtna_hastighet",
    "Hastighetsgräns",
    "Hastighetsgrans",
    "hastighetsgrans",
    "HTHAST",
    "HastighetsgransVarde",
    "maxspeed",
    "Maxspeed",
    "speed",
)


def limit_from_props(props: dict) -> "int | None":
    for k in LIMIT_KEYS:
        if k in props and props[k] not in (None, ""):
            try:
                v = int(round(float(str(props[k]).split()[0])))
            except (ValueError, IndexError):
                continue
            if 5 <= v <= 130:
                return v
    return None


def densify(coords: "list[tuple[float, float]]", step_m: float):
    """Lagger ut punkter langs en linje med jamna mellanrum.

    Enheten sokar narmaste punkt, inte narmaste linje - ett punktmoln ar mycket
    enklare att binarsoka i an en linjegeometri, och pa en vag ar skillnaden ingen
    sa lange punkterna ligger tatare an sokradien."""
    if not coords:
        return
    yield coords[0]
    carry = 0.0
    for (lon1, lat1), (lon2, lat2) in zip(coords, coords[1:]):
        # Plan approximation: pa nagra tiotal meter i Sverige ar felet forsumbart,
        # och alternativet vore dyrare utan att bli battre.
        mlat = math.radians((lat1 + lat2) / 2)
        dx = (lon2 - lon1) * 111320.0 * math.cos(mlat)
        dy = (lat2 - lat1) * 110540.0
        seg = math.hypot(dx, dy)
        if seg <= 0:
            continue
        t = (step_m - carry) / seg
        while t <= 1.0:
            yield (lon1 + (lon2 - lon1) * t, lat1 + (lat2 - lat1) * t)
            t += step_m / seg
        carry = (carry + seg) % step_m
    yield coords[-1]


WKT_LINESTRING = re.compile(r"LINESTRING\s*Z?\s*\((.*)\)", re.IGNORECASE)


def parse_wkt_line(wkt: str) -> "list[tuple[float, float]]":
    """"LINESTRING Z (lon lat z, lon lat z, ...)" -> [(lon, lat), ...].

    Ordningen ar lon fore lat, precis som i kamerornas punkter."""
    m = WKT_LINESTRING.search(wkt or "")
    if not m:
        return []
    out = []
    for part in m.group(1).split(","):
        nums = part.split()
        if len(nums) >= 2:
            try:
                out.append((float(nums[0]), float(nums[1])))
            except ValueError:
                pass
    return out


def fetch_limits_from_api(key: str, step_m: float) -> "list[tuple[float, float, int]]":
    """Hamtar hela Sveriges hastighetsgranser ur oppna API:et.

    NVDB-datat ligger i namespace vagdata.nvdb_dk_o och pagineras med
    changeid: varje svar bar ett LASTCHANGEID som ar nasta sidas start.
    Samma nyckel som till kamerorna fungerar - inget konto, ingen Lastkajen."""
    points: "list[tuple[float, float, int]]" = []
    change = "0"
    prev_change = None
    page = 0
    limit = 20000

    while True:
        page += 1
        # Objekttypen heter Hastighetsgr\u00e4ns med a-prickar, och API:et vill ha
        # den precis sa. Kroppen skickas som utf-8, vilket ar vad
        # Content-Type-huvudet lovar.
        body = (
            f'<REQUEST><LOGIN authenticationkey="{key}"/>'
            f'<QUERY objecttype="Hastighetsgr\u00e4ns" '
            f'namespace="v\u00e4gdata.nvdb_dk_o" schemaversion="1.2" '
            f'changeid="{change}" limit="{limit}">'
            f'<INCLUDE>H\u00f6gsta_till\u00e5tna_hastighet</INCLUDE>'
            f'<INCLUDE>Geometry.WKT-WGS84-3D</INCLUDE>'
            f'<INCLUDE>Deleted</INCLUDE><INCLUDE>Valid_To</INCLUDE>'
            f'</QUERY></REQUEST>'
        ).encode("utf-8")

        req = urllib.request.Request(
            API, data=body, headers={"Content-Type": "text/xml"}
        )
        with urllib.request.urlopen(req, timeout=300) as resp:
            payload = json.load(resp)

        result = payload["RESPONSE"]["RESULT"][0]
        if "ERROR" in result:
            raise SystemExit(f"Trafikverket: {result['ERROR']}")
        rows = result.get("Hastighetsgr\u00e4ns", [])
        change = result.get("INFO", {}).get("LASTCHANGEID", "")

        for row in rows:
            points.extend(limit_points_from_row(row, step_m))

        print(f"  sida {page}: {len(rows)} poster, {len(points)} punkter hittills")

        # Slutet ar en TOM sida eller ett changeid som star still - inte en
        # sida med farre poster an begart. En changeid-sida foljer interna
        # andringsklumpar och kan vara ofull mitt i datamangden, sa den som
        # stannar dar far med sig en brakdel och marker det inte. Det ar
        # skillnaden mellan 239 000 strackor och alla.
        if len(rows) == 0 or not change or change == prev_change:
            break
        prev_change = change

    return points


def limit_points_from_row(row: dict, step_m: float) -> "list[tuple[float, float, int]]":
    """En API-post -> punkter langs strackan. Borttagna och utgangna hoppas
    over: en grans som slutat galla ar varre an ingen."""
    if row.get("Deleted") in (True, "true", 1):
        return []
    valid_to = str(row.get("Valid_To", "9999"))
    if valid_to < "2026":
        return []

    limit = limit_from_props(row)
    if limit is None:
        return []

    geom = row.get("Geometry") or {}
    wkt = geom.get("WKT-WGS84-3D") or geom.get("WKT-WGS84")
    coords = parse_wkt_line(wkt)
    out = []
    for lon, lat in densify(coords, step_m):
        if in_sweden(lat, lon):
            out.append((lat, lon, limit))
    return out


def load_limit_points(path: str, step_m: float) -> "list[tuple[float, float, int]]":
    """Laser en NVDB-export och returnerar (lat, lon, hastighet)."""
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)

    features = data.get("features", data if isinstance(data, list) else [])
    points: "list[tuple[float, float, int]]" = []
    skipped = 0

    for feat in features:
        props = feat.get("properties", {}) or {}
        limit = limit_from_props(props)
        if limit is None:
            skipped += 1
            continue

        geom = feat.get("geometry") or {}
        gtype = geom.get("type")
        coords = geom.get("coordinates") or []

        lines: "list[list]" = []
        if gtype == "LineString":
            lines = [coords]
        elif gtype == "MultiLineString":
            lines = coords
        elif gtype == "Point":
            lines = [[coords]]
        else:
            continue

        for line in lines:
            clean = [(c[0], c[1]) for c in line if len(c) >= 2]
            for lon, lat in densify(clean, step_m):
                if in_sweden(lat, lon):
                    points.append((lat, lon, limit))

    if skipped:
        print(f"  {skipped} objekt utan hastighet - hoppade over")
    return points


class LimitIndex:
    """Rutnat over hastighetspunkter, for att hitta den narmaste utan att jamfora
    mot alla. Cellerna ar en hundradels grad, ungefar en kilometer, sa en sokning
    ror nio celler och nagra tiotal punkter i stallet for hundratusentals."""

    CELL = 0.01

    def __init__(self, points: "list[tuple[float, float, int]]"):
        self.grid: "dict[tuple[int, int], list[tuple[float, float, int]]]" = {}
        for lat, lon, lim in points:
            key = (int(lat / self.CELL), int(lon / self.CELL))
            self.grid.setdefault(key, []).append((lat, lon, lim))
        self.size = len(points)

    def nearest(self, lat: float, lon: float, radius_m: float) -> int:
        best = 0
        bestd = radius_m
        clat, clon = int(lat / self.CELL), int(lon / self.CELL)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                for plat, plon, lim in self.grid.get((clat + dy, clon + dx), ()):
                    mlat = math.radians(lat)
                    ddx = (plon - lon) * 111320.0 * math.cos(mlat)
                    ddy = (plat - lat) * 110540.0
                    d = math.hypot(ddx, ddy)
                    if d < bestd:
                        bestd = d
                        best = lim
        return best


# ---------------------------------------------------------------- kommandon --


def cmd_kameror(args: argparse.Namespace) -> None:
    key = args.key or os.environ.get("TRV_API_KEY")
    if not key:
        raise SystemExit("Ingen API-nyckel. Satt TRV_API_KEY eller anvand --key.")

    rows = query(key, args.objekttyp, args.schema, args.limit)
    print(f"{len(rows)} poster fran {args.objekttyp}")

    if args.visa_falt:
        print("\nForsta posten:")
        print(json.dumps(rows[0], indent=2, ensure_ascii=False))
        print(
            "\nFalten objekttypen ska ha enligt schemat: ID, Name, Bearing, "
            "Geometry.WGS84, Geometry.SWEREF99TM, RoadNumber, Counties, IconId, "
            "Deleted, ModifiedTime. Nagon skyltad hastighet finns inte."
        )
        return

    # Hastigheten bakas in har om en NVDB-fil anges. Det ar enda vagen dit:
    # objekttypen bar ingen skyltad hastighet alls. Utan inbakning star kameran
    # utan siffra pa skarmen, och gransen far komma ur HASTIGHET.BIN eller inte
    # alls.
    index = None
    if args.granser:
        print(f"bakar in hastigheter fran {args.granser} ...")
        index = LimitIndex(load_limit_points(args.granser, args.steg))
        print(f"  {index.size} referenspunkter")

    out: "list[tuple[int, int, int, int, int]]" = []
    utan_position = 0
    utan_riktning = 0
    utan_hastighet = 0
    borttagna = 0

    for row in rows:
        # API:et lamnar kvar borttagna objekt sa att den som synkar med changeid
        # ska fa veta att de forsvunnit. For oss som hamtar allt pa en gang ar de
        # bara nedmonterade kameror - och att varna for en nedmonterad kamera ar
        # varre an att inte varna alls, eftersom det ar sadant som far en att
        # sluta lita pa varningarna.
        if deep_get(row, ("Deleted", "deleted")) in (True, "true", "True", 1):
            borttagna += 1
            continue

        point = parse_point(deep_get(row, ("Geometry", "geometry")))
        if point is None:
            utan_position += 1
            continue
        lat, lon = point

        bearing = deep_get(row, ("Bearing", "bearing", "Direction"))
        if bearing is None:
            b = BEARING_UNKNOWN
            utan_riktning += 1
        else:
            b = int(round(float(bearing))) % 360

        lim = index.nearest(lat, lon, args.grans_radie) if index else 0
        if lim == 0:
            utan_hastighet += 1

        # ATK-stracka betyder att snittfarten mats mellan tva punkter, inte farten
        # just vid kameran. Det ar en annan sak att varna for, sa den markeras.
        # IconId ar faltet som skiljer kameratyperna; vilka varden det antar ar
        # inte dokumenterat, sa vi letar aven i postens ovriga text.
        icon = str(deep_get(row, ("IconId", "iconId")) or "").lower()
        text = json.dumps(row, ensure_ascii=False).lower()
        average = any(
            m in icon for m in ("stracka", "sträcka", "average", "section")
        ) or ("atk-stracka" in text or "atk-sträcka" in text)

        out.append(
            (
                int(round(lat * 1e7)),
                int(round(lon * 1e7)),
                b,
                lim,
                FLAG_AVERAGE_SPEED if average else 0,
            )
        )

    if not out:
        raise SystemExit(
            "Ingen post hade en position vi kunde tolka. Kor med --visa-falt for "
            "att se vad som faktiskt kom."
        )

    # Sorteringen ar inte kosmetisk: enheten binarsoker pa latitud.
    out.sort(key=lambda r: r[0])

    packed = [struct.pack("<iiHBB", *r) for r in out]
    write_table(args.ut, CAM_MAGIC, 12, packed)

    print(f"skrev {args.ut}: {len(packed)} kameror, {len(packed) * 12 + 12} byte")
    if borttagna:
        print(f"  {borttagna} markerade som borttagna - hoppade over")
    if utan_position:
        print(f"  {utan_position} utan tolkbar position - hoppade over")
    if utan_riktning:
        print(f"  {utan_riktning} utan riktning - varnas i bada korriktningar")
    if utan_hastighet:
        vad = "utan trafferad grans" if index else "utan hastighet (ingen --granser)"
        print(f"  {utan_hastighet} {vad} - visas utan siffra")


def cmd_granser(args: argparse.Namespace) -> None:
    if args.api:
        key = args.key or os.environ.get("TRV_API_KEY")
        if not key:
            raise SystemExit("Ingen API-nyckel. Satt TRV_API_KEY eller anvand --key.")
        print("hamtar hastighetsgranser fran Trafikverkets oppna API ...")
        points = fetch_limits_from_api(key, args.steg)
    else:
        print(f"laser {args.infil} ...")
        points = load_limit_points(args.infil, args.steg)
    if not points:
        raise SystemExit(
            "Ingen hastighet gick att lasa. Kolla vilket falt som bar hastigheten "
            "i din fil och lagg till det i LIMIT_KEYS."
        )

    rows = sorted(
        (int(round(lat * 1e7)), int(round(lon * 1e7)), lim) for lat, lon, lim in points
    )

    # Punkter som ligger pa varandra med samma hastighet ger inget - de kostar
    # bara plats pa kortet och tid i varje sokning.
    deduped: "list[tuple[int, int, int]]" = []
    for p in rows:
        if (
            deduped
            and deduped[-1][2] == p[2]
            and abs(deduped[-1][0] - p[0]) < 30
            and abs(deduped[-1][1] - p[1]) < 60
        ):
            continue
        deduped.append(p)

    packed = [struct.pack("<iiBB", lat, lon, lim, 0) for lat, lon, lim in deduped]
    write_table(args.ut, LIMIT_MAGIC, 10, packed)

    mb = (len(packed) * 10 + 12) / (1024 * 1024)
    print(f"skrev {args.ut}: {len(packed)} punkter, {mb:.1f} MB")
    if len(rows) != len(deduped):
        print(f"  {len(rows) - len(deduped)} overflodiga punkter togs bort")


def main() -> None:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = ap.add_subparsers(dest="cmd", required=True)

    k = sub.add_parser("kameror", help="hamtar fartkameror fran Trafikverket")
    k.add_argument("--key", help="API-nyckel (annars TRV_API_KEY)")
    k.add_argument("--ut", default="KAMEROR.BIN")
    k.add_argument("--objekttyp", default="TrafficSafetyCamera")
    k.add_argument("--schema", default="1")
    k.add_argument("--limit", type=int, default=20000)
    k.add_argument(
        "--granser",
        help="NVDB-GeoJSON att baka in skyltad hastighet ur (kameraposterna "
        "saknar hastighet i API:et)",
    )
    k.add_argument("--steg", type=float, default=40.0, help=argparse.SUPPRESS)
    k.add_argument(
        "--grans-radie",
        dest="grans_radie",
        type=float,
        default=120.0,
        help="hogsta avstand i meter mellan kamera och vagpunkt (standard 120)",
    )
    k.add_argument(
        "--visa-falt",
        action="store_true",
        help="skriver ut forsta posten och avslutar, for att se faltnamnen",
    )
    k.set_defaults(func=cmd_kameror)

    g = sub.add_parser("granser", help="bygger hastighetsfilen ur NVDB")
    g.add_argument(
        "--api",
        action="store_true",
        help="hamta direkt fran oppna API:et i stallet for fran en fil - "
        "samma nyckel som till kamerorna",
    )
    g.add_argument("--key", help="API-nyckel (annars TRV_API_KEY)")
    g.add_argument("--in", dest="infil", help="GeoJSON fran NVDB/Lastkajen")
    g.add_argument("--ut", default="HASTIGHET.BIN")
    g.add_argument(
        "--steg",
        type=float,
        default=40.0,
        help="meter mellan punkter langs vagen (standard 40)",
    )
    g.set_defaults(func=cmd_granser)

    args = ap.parse_args()
    if args.cmd == "granser" and not args.api and not args.infil:
        ap.error("granser kraver --api eller --in FIL")
    args.func(args)


if __name__ == "__main__":
    main()
