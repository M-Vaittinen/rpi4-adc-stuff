export const WS_URL = `ws://${window.location.hostname}:8765/ws`;

export const DEFAULT_SAMPLE_RATE = 4000;
export const DEFAULT_ADC_BITS = 16;
export const DEFAULT_REF_VOLTAGE = 3.3;
export const DEFAULT_Y_AXIS_MODE = "voltage";
export const MIN_ZOOM_SAMPLES = 500;
export const MAX_TRACE_POINTS = 200_000; // ~50s at 4 kSa/s

// Channel definitions — add entries here to create new plots.
// Binary data from the server is assumed interleaved: sample[i] → channel[i % N].
export const CHANNELS = [
  { id: 0, name: "CH1", color: "#39ff6e" },
  // { id: 1, name: "CH2", color: "#e06c75" },
  // { id: 2, name: "CH3", color: "#61afef" },
  // { id: 3, name: "CH4", color: "#e5c07b" },
];

export function makeTraceTemplate(color) {
  return {
    type: "scattergl",
    mode: "lines",
    line: { color, width: 1 },
  };
}

// Keep for backward compat if needed
export const TRACE_TEMPLATE = makeTraceTemplate(CHANNELS[0].color);

export function makeLayout(yAxisMode, refVoltage, adcBits) {
  const maxAdc = adcBits === 16 ? 65535 : 4095;
  const yTitle = yAxisMode === "voltage" ? "Voltage (V)" : "Raw ADC value";
  const yMax = yAxisMode === "voltage" ? refVoltage : maxAdc;

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
      title: { text: yTitle, font: { color: "#585b70", size: 14 } },
      color: "#585b70",
      gridcolor: "#1a1a2e",
      zerolinecolor: "#1a1a2e",
      tickfont: { size: 12 },
      minallowed: 0,
      maxallowed: yMax,
      autorange: true,
    },
    font: { family: "JetBrains Mono, Fira Code, monospace" },
    dragmode: "pan",
  };
}
