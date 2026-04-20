declare const __APP_VERSION__: string;
export const APP_VERSION = __APP_VERSION__;

//export const WS_URL = `ws://${window.location.hostname}:8765/ws`; // when on same device
export const WS_URL = `ws://adc-rohm2.local:8765/ws`;
export const NOT_IMPLEMENTED = true;

// mvaring chunk layout (must match struct adc_data in C code)
export const MAX_SAMPS = 1024;
export const CHUNK_BYTES = 4 + MAX_SAMPS * 4 + MAX_SAMPS * 4;

// CPU / GPU buffer pre-allocation
export const INIT_CAP = 1_000_000;
export const INIT_GPU_CAP = 1_000_000;

// Plot interaction
export const ZOOM_FACTOR = 1.15;
export const MIN_VISIBLE = 10; // minimum samples visible when zoomed in

// Streaming defaults
export const DEFAULT_SAMPLE_RATE = 200_000;
export const LIVE_WINDOW_SIZE = 50_000;
export const LIVE_WINDOW_MIN = 100;
export const LIVE_WINDOW_MAX = 1_000_000;
export const LIVE_WINDOW_STEP_SIZE = 100;

// ADC bit-depth options (value = max raw count)
export const ADC_OPTIONS = [
  { label: "16-bit", value: 65535 },
  { label: "12-bit", value: 4095 },
] as const;
export const DEFAULT_ADC_MAX = ADC_OPTIONS[1].value;
