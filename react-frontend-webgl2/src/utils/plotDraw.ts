import type { View, HoverPhys } from "../types";
import type { ThemeColors } from "./themeColors";

export const PAD = { l: 62, b: 34, t: 10, r: 12 } as const;

function timeUnit(rangeS: number): [number, string] {
  return rangeS < 0.001 ? [1e6, "µs"] : rangeS < 1 ? [1e3, "ms"] : [1, "s"];
}

export function drawAxes(
  ctx: CanvasRenderingContext2D,
  W: number,
  H: number,
  dpr: number,
  count: number,
  xMin: number,
  xMax: number,
  sampleRate: number,
  adcMax: number,
  colors: ThemeColors,
): void {
  const pl = PAD.l * dpr,
    pb = PAD.b * dpr,
    pt = PAD.t * dpr,
    pr = PAD.r * dpr;
  const pw = W - pl - pr;
  const ph = H - pt - pb;

  ctx.clearRect(0, 0, W, H);
  ctx.save();

  ctx.strokeStyle = colors.border;
  ctx.lineWidth = dpr;
  ctx.strokeRect(pl, pt, pw, ph);

  const fs = 11 * dpr;
  ctx.font = `${fs}px monospace`;
  ctx.fillStyle = colors.foreground;

  // Y-axis labels
  const yLabels = [0, 0.25, 0.5, 0.75, 1].map((f) => Math.round(f * adcMax));
  ctx.textAlign = "right";
  ctx.textBaseline = "middle";
  for (const v of yLabels) {
    const py = pt + ph * (1 - v / adcMax);
    ctx.fillText(String(v), pl - 5 * dpr, py);
    ctx.beginPath();
    ctx.moveTo(pl - 3 * dpr, py);
    ctx.lineTo(pl, py);
    ctx.stroke();
  }

  // X-axis labels
  ctx.textBaseline = "top";
  const N_X = 5;
  const xRange = Math.max(xMax - xMin, 1);
  const [mult, unit] = timeUnit(xRange / sampleRate);
  for (let i = 0; i <= N_X; i++) {
    const idx = xMin + (xRange * i) / N_X;
    const px = pl + pw * (i / N_X);
    const label = ((idx / sampleRate) * mult).toFixed(2) + " " + unit;
    ctx.textAlign = i === 0 ? "left" : i === N_X ? "right" : "center";
    ctx.fillText(label, px, pt + ph + 4 * dpr);
    ctx.beginPath();
    ctx.moveTo(px, pt + ph);
    ctx.lineTo(px, pt + ph + 3 * dpr);
    ctx.stroke();
  }

  // Sample count watermark
  ctx.textAlign = "left";
  ctx.textBaseline = "top";
  ctx.strokeStyle = "black";
  ctx.lineWidth = 3 * dpr;
  ctx.lineJoin = "round";
  const watermark = `n = ${count.toLocaleString()}`;
  const wx = pl + 4 * dpr;
  const wy = pt + 2 * dpr;
  ctx.strokeText(watermark, wx, wy);
  ctx.fillStyle = colors.foreground;
  ctx.fillText(watermark, wx, wy);

  ctx.restore();
}

export function drawCrosshair(
  ctx: CanvasRenderingContext2D,
  W: number,
  H: number,
  dpr: number,
  ys: Float32Array,
  count: number,
  view: View,
  hoverPhys: HoverPhys,
  sampleRate: number,
  adcMax: number,
  colors: ThemeColors,
): void {
  const pl = PAD.l * dpr,
    _pb = PAD.b * dpr,
    pt = PAD.t * dpr,
    _pr = PAD.r * dpr;
  const pw = W - pl - _pr;
  const ph = H - pt - _pb;

  ctx.clearRect(0, 0, W, H);
  if (!hoverPhys || count < 2 || pw <= 0 || ph <= 0) return;

  const { x: hx, y: hy } = hoverPhys;
  if (hx < pl || hx > pl + pw || hy < pt || hy > pt + ph) return;

  // cursor → nearest sample
  const t = (hx - pl) / pw;
  const sampleF = view.xMin + t * (view.xMax - view.xMin);
  const sampleIdx = Math.round(Math.max(0, Math.min(count - 1, sampleF)));
  const yVal = ys[sampleIdx] ?? 0;

  // physical coords of the actual data point
  const dataX = pl + pw * ((sampleIdx - view.xMin) / (view.xMax - view.xMin));
  const dataY = pt + ph * (1 - yVal / adcMax);

  ctx.save();
  ctx.lineWidth = dpr;
  ctx.setLineDash([4 * dpr, 4 * dpr]);
  ctx.strokeStyle = colors.foreground20;

  // vertical line
  ctx.beginPath();
  ctx.moveTo(hx, pt);
  ctx.lineTo(hx, pt + ph);
  ctx.stroke();

  // horizontal line at actual y
  ctx.beginPath();
  ctx.moveTo(pl, dataY);
  ctx.lineTo(pl + pw, dataY);
  ctx.stroke();
  ctx.setLineDash([]);

  // dot at (sampleIdx, yVal)
  ctx.fillStyle = colors.foreground;
  ctx.strokeStyle = colors.foreground60;
  ctx.lineWidth = 1.5 * dpr;
  ctx.beginPath();
  ctx.arc(dataX, dataY, 4 * dpr, 0, Math.PI * 2);
  ctx.fill();
  ctx.stroke();

  // tooltip
  const fs = 11 * dpr;
  ctx.font = `${fs}px monospace`;
  const [mult, unit] = timeUnit((view.xMax - view.xMin) / sampleRate);
  const timeLabel = ((sampleIdx / sampleRate) * mult).toFixed(2) + " " + unit;
  const label = `x: ${timeLabel}  y: ${Math.round(yVal)}`;
  const tw = ctx.measureText(label).width + 14 * dpr;
  const th = fs + 10 * dpr;
  let tx = hx + 14 * dpr;
  let ty = dataY - th / 2;
  if (tx + tw > pl + pw) tx = hx - tw - 14 * dpr;
  ty = Math.max(pt + 2 * dpr, Math.min(pt + ph - th - 2 * dpr, ty));

  ctx.fillStyle = colors.popover90;
  ctx.strokeStyle = colors.border;
  ctx.lineWidth = dpr;
  ctx.fillRect(tx, ty, tw, th);
  ctx.strokeRect(tx, ty, tw, th);

  ctx.fillStyle = colors.popoverForeground;
  ctx.textAlign = "left";
  ctx.textBaseline = "middle";
  ctx.fillText(label, tx + 7 * dpr, ty + th / 2);

  ctx.restore();
}
