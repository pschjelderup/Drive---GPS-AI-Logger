// Handritade SVG-diagram efter dataviz-metoden: tunna staplar med rundade
// dataandar forankrade i baslinjen, 2 px-linjer, aterhallsamt rutnat i egen
// farg, text i textfarger (aldrig seriefargen), och en svavarruta pa allt -
// ett HTML-diagram AR interaktivt. En axel per diagram, alltid.
import { useRef, useState } from "react";
import {
  GRID, BASELINE, INK_MUTED, INK,
} from "../lib/palette.js";

const W = 720;
const H = 220;
const PAD = { l: 44, r: 12, t: 12, b: 26 };

function niceMax(v) {
  if (v <= 0) return 1;
  const pow = 10 ** Math.floor(Math.log10(v));
  for (const m of [1, 2, 5, 10]) {
    if (m * pow >= v) return m * pow;
  }
  return 10 * pow;
}

function useTooltip() {
  const [tip, setTip] = useState(null);
  const boxRef = useRef(null);
  const show = (evt, text) => {
    const host = boxRef.current?.getBoundingClientRect();
    if (!host) return;
    setTip({ x: evt.clientX - host.left, y: evt.clientY - host.top, text });
  };
  return { tip, boxRef, show, hide: () => setTip(null) };
}

function Frame({ boxRef, tip, children }) {
  return (
    <div ref={boxRef} style={{ position: "relative", overflowX: "auto" }}>
      {children}
      {tip && (
        <div className="charttip" style={{ left: tip.x, top: tip.y }}>
          {tip.text}
        </div>
      )}
    </div>
  );
}

function GridLines({ max, unit }) {
  const rows = [0, 0.25, 0.5, 0.75, 1];
  return (
    <g>
      {rows.map((f) => {
        const y = PAD.t + (H - PAD.t - PAD.b) * (1 - f);
        return (
          <g key={f}>
            {f > 0 && (
              <line x1={PAD.l} x2={W - PAD.r} y1={y} y2={y}
                stroke={GRID} strokeWidth="1" />
            )}
            <text x={PAD.l - 6} y={y + 3} textAnchor="end" fontSize="10"
              fill={INK_MUTED}>
              {Math.round(max * f)}{f === 1 && unit ? ` ${unit}` : ""}
            </text>
          </g>
        );
      })}
      <line x1={PAD.l} x2={W - PAD.r}
        y1={H - PAD.b} y2={H - PAD.b} stroke={BASELINE} strokeWidth="1" />
    </g>
  );
}

// Staplar: en per resa. items = [{label, value, color, tip}]
export function BarChart({ items, unit }) {
  const { tip, boxRef, show, hide } = useTooltip();
  if (!items.length) return <p className="status">ingen data än</p>;

  const max = niceMax(Math.max(...items.map((d) => d.value)));
  const innerW = W - PAD.l - PAD.r;
  const innerH = H - PAD.t - PAD.b;
  // Tunna staplar med minst 2 px yta emellan, aldrig bredare an 26 px.
  const step = innerW / items.length;
  const bw = Math.min(26, Math.max(3, step - 2));
  const every = Math.ceil(items.length / 10);

  return (
    <Frame boxRef={boxRef} tip={tip}>
      <svg viewBox={`0 0 ${W} ${H}`} style={{ width: "100%", minWidth: 480 }}>
        <GridLines max={max} unit={unit} />
        {items.map((d, i) => {
          const h = Math.max(d.value > 0 ? 3 : 0, (d.value / max) * innerH);
          const x = PAD.l + i * step + (step - bw) / 2;
          const y = H - PAD.b - h;
          return (
            <g key={i}>
              <rect x={x} y={y} width={bw} height={h} fill={d.color}
                rx="4" ry="4" />
              {/* rundningen ska sitta i dataanden, inte i baslinjen */}
              {h > 4 && (
                <rect x={x} y={H - PAD.b - Math.min(h, 4)} width={bw}
                  height={Math.min(h, 4)} fill={d.color} />
              )}
              {/* traffytan ar storre an stapeln */}
              <rect x={PAD.l + i * step} y={PAD.t} width={step} height={innerH}
                fill="transparent"
                onMouseMove={(e) => show(e, d.tip)}
                onMouseLeave={hide} />
              {i % every === 0 && (
                <text x={x + bw / 2} y={H - PAD.b + 14} textAnchor="middle"
                  fontSize="9" fill={INK_MUTED}>{d.label}</text>
              )}
            </g>
          );
        })}
      </svg>
    </Frame>
  );
}

// Linje: en serie over tid. items = [{label, value, tip}]
export function LineChart({ items, unit, color, domainMax }) {
  const { tip, boxRef, show, hide } = useTooltip();
  const [hoverI, setHoverI] = useState(null);
  if (items.length < 2) return <p className="status">för lite data än</p>;

  const max = domainMax ?? niceMax(Math.max(...items.map((d) => d.value)));
  const innerW = W - PAD.l - PAD.r;
  const innerH = H - PAD.t - PAD.b;
  const px = (i) => PAD.l + (i / (items.length - 1)) * innerW;
  const py = (v) => PAD.t + innerH * (1 - v / max);
  const path = items.map((d, i) => `${i ? "L" : "M"}${px(i)},${py(d.value)}`).join(" ");
  const every = Math.ceil(items.length / 10);

  return (
    <Frame boxRef={boxRef} tip={tip}>
      <svg viewBox={`0 0 ${W} ${H}`} style={{ width: "100%", minWidth: 480 }}
        onMouseLeave={() => { hide(); setHoverI(null); }}>
        <GridLines max={max} unit={unit} />
        {/* harkors + punkt vid svavning */}
        {hoverI != null && (
          <line x1={px(hoverI)} x2={px(hoverI)} y1={PAD.t} y2={H - PAD.b}
            stroke={INK_MUTED} strokeWidth="1" strokeDasharray="3 3" />
        )}
        <path d={path} fill="none" stroke={color} strokeWidth="2"
          strokeLinejoin="round" strokeLinecap="round" />
        {hoverI != null && (
          <circle cx={px(hoverI)} cy={py(items[hoverI].value)} r="4"
            fill={color} stroke={INK} strokeWidth="1.5" />
        )}
        {items.map((d, i) => (
          i % every === 0 && (
            <text key={i} x={px(i)} y={H - PAD.b + 14} textAnchor="middle"
              fontSize="9" fill={INK_MUTED}>{d.label}</text>
          )
        ))}
        <rect x={PAD.l} y={PAD.t} width={innerW} height={innerH}
          fill="transparent"
          onMouseMove={(e) => {
            const svg = e.currentTarget.ownerSVGElement;
            const r = svg.getBoundingClientRect();
            const fx = ((e.clientX - r.left) / r.width) * W;
            const i = Math.round(((fx - PAD.l) / innerW) * (items.length - 1));
            const ci = Math.max(0, Math.min(items.length - 1, i));
            setHoverI(ci);
            show(e, items[ci].tip);
          }} />
      </svg>
    </Frame>
  );
}
