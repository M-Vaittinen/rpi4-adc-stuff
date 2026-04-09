//export const WS_URL = `ws://${window.location.hostname}:8765/ws`; // when on same device
export const WS_URL = `ws://192.168.255.1:8765/ws`;

export const DEFAULT_SAMPLE_RATE = 100_000;
// export const DEFAULT_SAMPLE_RATE = 4000; // simulation
export const DEFAULT_ADC_BITS = 16;
export const DEFAULT_MAX_VOLTAGE = 3.3;
export const DEFAULT_Y_AXIS_MODE = "raw"; // voltage / raw
export const MAX_RAW_POINTS = 1_000_000; // raw sample ring-buffer (~10s at 100 kSa/s)
export const MAX_RENDER_POINTS = 250_000; // display points after LTTB downsampling
export const RENDER_INTERVAL_MS = 100; // downsample + render period (ms, ~10 fps)
export const VIEW_WINDOW_S = 0.25; // seconds visible in live-follow mode

export const PLOT0 = { id: 0, name: "CH1", color: "#39ff6e" };

/**
 * Compute custom tick positions and labels scaled to the best time unit
 * for the current visible range. Returns { tickvals, ticktext, title }.
 */
export function computeXAxisTicks(x0, x1) {
  const span = x1 - x0;

  let factor, unit;
  if (span < 1e-3) {
    factor = 1e6;
    unit = "µs";
  } else if (span < 1) {
    factor = 1e3;
    unit = "ms";
  } else {
    factor = 1;
    unit = "s";
  }

  const dispSpan = span * factor;
  const rawStep = dispSpan / 6;
  const mag = Math.pow(10, Math.floor(Math.log10(rawStep)));
  const res = rawStep / mag;
  const niceStep =
    res <= 1.5 ? mag : res <= 3.5 ? 2 * mag : res <= 7.5 ? 5 * mag : 10 * mag;

  const dispX0 = x0 * factor;
  const dispX1 = x1 * factor;
  const tickStart = Math.ceil(dispX0 / niceStep) * niceStep;

  const decimals =
    niceStep >= 1 ? 0 : Math.max(0, Math.ceil(-Math.log10(niceStep)));

  const tickvals = [];
  const ticktext = [];
  for (let d = tickStart; d <= dispX1 + niceStep * 0.01; d += niceStep) {
    tickvals.push(d / factor);
    ticktext.push(d.toFixed(decimals) + " " + unit);
  }

  return { tickvals, ticktext, title: `Time (${unit})` };
}

export function makeTraceTemplate(color) {
  return {
    type: "scattergl",
    mode: "lines",
    line: { color, width: 1 },
  };
}

export const plotlyModeBarButtonsToRemove = [
  "zoom2d",
  "zoomIn2d",
  "zoomOut2d",
  "pan2d",
  "resetScale2d",
  "autoScale2d",
];

export const autoScaleIcon = {
  width: 1000,
  height: 1000,
  path: "m250 850l-187 0-63 0 0-62 0-188 63 0 0 188 187 0 0 62z m688 0l-188 0 0-62 188 0 0-188 62 0 0 188 0 62-62 0z m-875-938l0 188-63 0 0-188 0-62 63 0 187 0 0 62-187 0z m875 188l0-188-188 0 0-62 188 0 62 0 0 62 0 188-62 0z m-125 188l-1 0-93-94-156 156 156 156 92-93 2 0 0 250-250 0 0-2 93-92-156-156-156 156 94 92 0 2-250 0 0-250 0 0 93 93 157-156-157-156-93 94 0 0 0-250 250 0 0 0-94 93 156 157 156-157-93-93 0 0 250 0 0 250z",
  transform: "matrix(1 0 0 -1 0 850)",
};

export function makeLayout(adcBits) {
  const maxAdc = adcBits === 16 ? 65535 : 4095;

  return {
    paper_bgcolor: "rgba(0,0,0,0)",
    plot_bgcolor: "#000000",
    margin: { t: 10, r: 16, b: 40, l: 60 },
    xaxis: {
      title: { text: "Time (s)", font: { color: "#585b70", size: 14 } },
      color: "#585b70",
      gridcolor: "#1a1a2e",
      zerolinecolor: "#1a1a2e",
      tickfont: { size: 12 },
      minallowed: 0,
    },
    yaxis: {
      title: { text: "Raw ADC value", font: { color: "#585b70", size: 14 } },
      color: "#585b70",
      gridcolor: "#1a1a2e",
      zerolinecolor: "#1a1a2e",
      tickfont: { size: 12 },
      minallowed: 0,
      maxallowed: maxAdc,
      autorange: true,
      fixedrange: true,
    },
    font: { family: "JetBrains Mono, Fira Code, monospace" },
    dragmode: false,
  };
}
