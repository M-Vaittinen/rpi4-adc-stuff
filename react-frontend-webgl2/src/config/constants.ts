//export const WS_URL = `ws://${window.location.hostname}:8765/ws`; // when on same device
export const WS_URL = `ws://192.168.255.1:8765/ws`;

// CPU / GPU buffer pre-allocation
export const INIT_CAP = 1_000_000;
export const INIT_GPU_CAP = 1_000_000;

// Plot interaction
export const ZOOM_FACTOR = 1.15;
export const MIN_VISIBLE = 10; // minimum samples visible when zoomed in

// Streaming defaults
export const DEFAULT_SAMPLE_RATE = 100_000;
export const LIVE_WINDOW_SIZE = 50_000;
export const LIVE_WINDOW_MIN = 1_000;
export const LIVE_WINDOW_MAX = 1_000_000;

// ADC bit-depth options (value = max raw count)
export const ADC_OPTIONS = [
  { label: "16-bit", value: 65535 },
  { label: "12-bit", value: 4095 },
  { label: "11-bit", value: 2047 },
] as const;
export const DEFAULT_ADC_MAX = ADC_OPTIONS[0].value;
