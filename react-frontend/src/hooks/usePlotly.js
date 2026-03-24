import { useState, useEffect, useRef, useCallback } from "react";
import Plotly from "plotly.js-gl2d-dist-min";
import {
  MIN_ZOOM_SAMPLES,
  MAX_TRACE_POINTS,
  makeLayout,
  makeTraceTemplate,
} from "../config/constants";

export function usePlotly({ channel, yAxisMode, refVoltage, adcBits, sampleRate }) {
  const [sampleCount, setSampleCount] = useState(0);

  const plotRef = useRef(null);
  const plotInited = useRef(false);
  const indexRef = useRef(0);
  const rafRef = useRef(null);
  const pendingX = useRef([]);
  const pendingY = useRef([]);
  const layoutRef = useRef(makeLayout(yAxisMode, refVoltage, adcBits));
  const sampleRateRef = useRef(sampleRate);
  const traceTemplate = useRef(makeTraceTemplate(channel.color));

  useEffect(() => { sampleRateRef.current = sampleRate; }, [sampleRate]);

  // Update layout when settings change
  useEffect(() => {
    layoutRef.current = makeLayout(yAxisMode, refVoltage, adcBits);
    if (plotRef.current && plotInited.current) {
      Plotly.relayout(plotRef.current, layoutRef.current);
    }
  }, [yAxisMode, refVoltage, adcBits]);

  // Initialize Plotly chart
  useEffect(() => {
    const el = plotRef.current;
    if (!el) return;

    const timer = setTimeout(() => {
      Plotly.newPlot(
        el,
        [{ x: [], y: [], ...traceTemplate.current }],
        layoutRef.current,
        {
          responsive: true,
          displayModeBar: false,
          scrollZoom: true,
        },
      );

      const MIN_X_SPAN = MIN_ZOOM_SAMPLES / sampleRateRef.current;

      el.addEventListener(
        "wheel",
        (e) => {
          const xRange = el._fullLayout?.xaxis?.range;
          if (!xRange) return;
          const span = xRange[1] - xRange[0];

          // Block zoom-in past minimum span
          if (e.deltaY < 0 && span <= MIN_X_SPAN) {
            e.preventDefault();
            e.stopPropagation();
            return;
          }

          // Block zoom-out past data range
          if (e.deltaY > 0) {
            const traceX = el.data?.[0]?.x;
            if (traceX && traceX.length > 0) {
              const dataSpan = traceX[traceX.length - 1] - traceX[0];
              if (span >= dataSpan) {
                e.preventDefault();
                e.stopPropagation();
              }
            }
          }
        },
        { capture: true, passive: false },
      );

      plotInited.current = true;
    }, 0);

    return () => {
      clearTimeout(timer);
      Plotly.purge(el);
      plotInited.current = false;
    };
  }, []);

  // Flush pending data to Plotly on animation frame
  const flushToPlot = useCallback(() => {
    rafRef.current = null;
    if (pendingX.current.length === 0) return;

    const newX = pendingX.current;
    const newY = pendingY.current;
    pendingX.current = [];
    pendingY.current = [];

    if (plotRef.current) {
      Plotly.extendTraces(plotRef.current, { x: [newX], y: [newY] }, [0]);
    }
  }, []);

  // Periodic maintenance: trim trace, update axis bounds, update sample counter.
  useEffect(() => {
    const interval = setInterval(() => {
      setSampleCount(indexRef.current);

      if (!plotRef.current || !plotInited.current) return;

      const traceData = plotRef.current.data?.[0];
      if (traceData && traceData.x.length > MAX_TRACE_POINTS) {
        const excess = traceData.x.length - MAX_TRACE_POINTS;
        traceData.x.splice(0, excess);
        traceData.y.splice(0, excess);
      }

      Plotly.react(plotRef.current, plotRef.current.data, layoutRef.current);
    }, 2000);

    return () => clearInterval(interval);
  }, []);

  const resetPlot = useCallback(() => {
    indexRef.current = 0;
    setSampleCount(0);
    if (plotRef.current) {
      Plotly.react(
        plotRef.current,
        [{ x: [], y: [], ...traceTemplate.current }],
        layoutRef.current,
      );
    }
  }, []);

  return {
    plotRef,
    indexRef,
    pendingX,
    pendingY,
    rafRef,
    sampleCount,
    flushToPlot,
    resetPlot,
  };
}
