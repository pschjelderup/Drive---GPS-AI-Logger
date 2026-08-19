// Vaglagret: hela NVDB som punkter pa en canvas.
//
// Hastighetsfilen ar tva miljoner punkter - tusen ganger mer an Leaflet
// klarar som markorer. Darfor ritas de direkt pa en egen canvas: punkterna
// ligger latitudsorterade i sina typade falt, sa synfaltet plockas ut med
// binarsokning, projiceras med webbmercatorformeln inskriven har (en
// funktionsanrop per punkt hade kostat mer an sjalva ritandet), och glesas
// ut nar fler an ett par hundra tusen ar i bild. Utzoomat blir det en
// vagkarta av ren punkttathet; inzoomat star varje punkt for sig.
import L from "leaflet";

export function createRoadLayer(points, colorFor) {
  const { lat, lon, lim, n } = points;

  // Forsta index vars latitud (i 1e7-delar) ar >= varde.
  const lowerBound = (value) => {
    let lo = 0, hi = n;
    while (lo < hi) {
      const mid = (lo + hi) >>> 1;
      if (lat[mid] < value) lo = mid + 1;
      else hi = mid;
    }
    return lo;
  };

  const RoadLayer = L.Layer.extend({
    onAdd(map) {
      this._map = map;
      this._canvas = L.DomUtil.create("canvas", "leaflet-road-layer");
      this._canvas.style.pointerEvents = "none";
      // Forst i panelen: vagarna ar underlag och ska ligga under bade
      // gpx-sparen och kamerorna, oavsett i vilken ordning lagren slas pa.
      const pane = map.getPanes().overlayPane;
      pane.insertBefore(this._canvas, pane.firstChild);
      map.on("moveend zoomend resize", this._redraw, this);
      map.on("zoomanim", this._hide, this);
      this._redraw();
      return this;
    },

    onRemove(map) {
      map.off("moveend zoomend resize", this._redraw, this);
      map.off("zoomanim", this._hide, this);
      this._canvas.remove();
      this._canvas = null;
      this._map = null;
      return this;
    },

    _hide() {
      // Under zoomanimationen skulle punkterna sta kvar pa fel stalle och
      // "slapa efter" - hellre borta ett ogonblick an fel.
      if (this._canvas) this._canvas.style.opacity = "0";
    },

    _redraw() {
      const map = this._map;
      const canvas = this._canvas;
      if (!map || !canvas) return;

      const size = map.getSize();
      canvas.width = size.x;
      canvas.height = size.y;
      canvas.style.opacity = "0.8";
      L.DomUtil.setPosition(canvas, map.containerPointToLayerPoint([0, 0]));

      const ctx = canvas.getContext("2d");
      ctx.clearRect(0, 0, size.x, size.y);

      const b = map.getBounds().pad(0.02);
      const south = Math.round(b.getSouth() * 1e7);
      const north = Math.round(b.getNorth() * 1e7);
      const west = b.getWest();
      const east = b.getEast();

      const lo = lowerBound(south);
      const hi = lowerBound(north);
      if (hi <= lo) return;

      // Fler an ~300k punkter i bild ger inget for ogat men kostar tid -
      // da ritas varannan, var tredje osv. Tatheten forblir jamn eftersom
      // urglesningen sker i latitudordning.
      const stride = Math.max(1, Math.floor((hi - lo) / 300000));

      // Webbmercator, samma formel som Leaflets EPSG:3857: konstanterna
      // raknas en gang, sedan ar varje punkt tva multiplikationer.
      const zoom = map.getZoom();
      const scale = 256 * Math.pow(2, zoom);
      const origin = map.getPixelBounds().min;
      const DEG = Math.PI / 180;

      const dot = zoom >= 13 ? 3 : zoom >= 10 ? 2 : 1;
      let currentColor = null;

      for (let i = lo; i < hi; i += stride) {
        const lonDeg = lon[i] / 1e7;
        if (lonDeg < west || lonDeg > east) continue;

        const latDeg = lat[i] / 1e7;
        const x = (lonDeg / 360 + 0.5) * scale - origin.x;
        const sinY = Math.sin(latDeg * DEG);
        const y = (0.5 - Math.log((1 + sinY) / (1 - sinY)) / (4 * Math.PI)) *
          scale - origin.y;

        const c = colorFor(lim[i]);
        if (c !== currentColor) {
          ctx.fillStyle = c;
          currentColor = c;
        }
        ctx.fillRect(x, y, dot, dot);
      }
    },
  });

  return new RoadLayer();
}
