/** CPU-side sample buffer: Float32Array stores uint16 values losslessly. */
export interface PlotData {
  ys: Float32Array;
  count: number;
}

/** Visible sample-index range (real-valued, fractional). */
export interface View {
  xMin: number;
  xMax: number;
}

/** Physical-pixel cursor coordinates within the canvas, or null when not hovering. */
export type HoverPhys = { x: number; y: number } | null;
