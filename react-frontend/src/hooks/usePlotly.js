import { useState, useEffect, useRef, useCallback } from "react";
import Plotly from "plotly.js-gl2d-dist-min";
import {
  MIN_ZOOM_SAMPLES,
  MAX_RAW_POINTS,
  MAX_RENDER_POINTS,
  RENDER_INTERVAL_MS,
  VIEW_WINDOW_S,
  makeLayout,
  makeTraceTemplate,
} from "../config/constants";
import { lttbDownsample } from "../utils/downsample";

export function usePlotly({
  channel,
  yAxisMode,
  refVoltage,
  adcBits,
  sampleRate,
  streaming,
}) {
  const [sampleCount, setSampleCount] = useState(0);

  const plotRef = useRef(null);
  const plotInited = useRef(false);
  const indexRef = useRef(0);
  const rafRef = useRef(null);
  const pendingX = useRef([]);
  const pendingY = useRef([]);
  const rawX = useRef([]);
  const rawY = useRef([]);
  const layoutRef = useRef(makeLayout(yAxisMode, refVoltage, adcBits));
  const sampleRateRef = useRef(sampleRate);
  const traceTemplate = useRef(makeTraceTemplate(channel.color));
  const interactingUntil = useRef(0);
  const isDraggingRef = useRef(false);
  const streamingRef = useRef(streaming);
  const isFollowingRef = useRef(streaming); // only follow while actively streaming
  const viewRangeRef = useRef(null); // [x0, x1] of current viewport; always explicit
  const [isFollowing, setIsFollowing] = useState(streaming);

  // Keep streamingRef current; enter follow when streaming starts, leave when it stops
  useEffect(() => {
    streamingRef.current = streaming;
    if (streaming) {
      isFollowingRef.current = true;
      setIsFollowing(true);
    } else {
      isFollowingRef.current = false;
      setIsFollowing(false);
    }
  }, [streaming]);

  useEffect(() => {
    sampleRateRef.current = sampleRate;
  }, [sampleRate]);

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
        { ...layoutRef.current, dragmode: false }, // pan handled manually
        {
          responsive: true,
          displayModeBar: false,
          scrollZoom: false,
        },
      );

      const MIN_X_SPAN = MIN_ZOOM_SAMPLES / sampleRateRef.current;
      const ZOOM_FACTOR = 0.15;
      const INTERACTION_COOLDOWN = 400;

      // ── Manual pan ────────────────────────────────────────────────────────
      let dragStartX = null;
      let dragStartRange = null;

      el.addEventListener("mousedown", (e) => {
        if (e.button !== 0 || streamingRef.current) return;
        isDraggingRef.current = true;
        dragStartX = e.clientX;
        dragStartRange = el._fullLayout?.xaxis?.range?.slice() ?? null;
      });

      window.addEventListener("mousemove", (e) => {
        if (!isDraggingRef.current || !dragStartRange || streamingRef.current)
          return;
        interactingUntil.current = performance.now() + INTERACTION_COOLDOWN;

        const xAxis = el._fullLayout?.xaxis;
        if (!xAxis) return;

        const [r0, r1] = dragStartRange;
        const span = r1 - r0;
        const plotWidthPx = xAxis.l2p(r1) - xAxis.l2p(r0);
        if (plotWidthPx === 0) return;

        // Pixels dragged → seconds shifted (negative: dragging right moves view left)
        const dxS = -((e.clientX - dragStartX) / plotWidthPx) * span;
        const newX0 = r0 + dxS;
        const newX1 = r1 + dxS;

        viewRangeRef.current = [newX0, newX1];
        Plotly.relayout(el, {
          "xaxis.range": [newX0, newX1],
          "xaxis.autorange": false,
        });
      });

      window.addEventListener("mouseup", () => {
        if (isDraggingRef.current) {
          isDraggingRef.current = false;
          dragStartX = null;
          dragStartRange = null;
          interactingUntil.current = performance.now() + 150;
        }
      });

      // ── Manual scroll zoom ────────────────────────────────────────────────

      el.addEventListener(
        "wheel",
        (e) => {
          if (streamingRef.current) {
            e.preventDefault();
            e.stopPropagation();
            return;
          }
          e.preventDefault();
          e.stopPropagation();

          // Suppress data renders while user is actively scrolling
          interactingUntil.current = performance.now() + INTERACTION_COOLDOWN;

          // Leave following mode on any manual scroll
          if (isFollowingRef.current) {
            isFollowingRef.current = false;
            setIsFollowing(false);
          }

          const xAxis = el._fullLayout?.xaxis;
          if (!xAxis) return;
          const [x0, x1] = xAxis.range;
          const span = x1 - x0;

          // Find which data-x value is under the cursor
          const rect = el.getBoundingClientRect();
          const plotLeft = xAxis._offset ?? 0;
          const plotWidth = xAxis._length ?? rect.width;
          const mousePx = e.clientX - rect.left - plotLeft;
          const mouseFrac = Math.max(0, Math.min(1, mousePx / plotWidth));
          const cursorX = x0 + mouseFrac * span;

          // Zoom around the cursor (cursorX stays fixed on screen)
          const zoomingIn = e.deltaY < 0;
          const delta = zoomingIn ? -ZOOM_FACTOR : ZOOM_FACTOR;
          const minSpan = MIN_ZOOM_SAMPLES / sampleRate;
          const newSpan = Math.max(minSpan, span * (1 + delta));

          const newX0 = cursorX - mouseFrac * newSpan;
          const newX1 = newX0 + newSpan;

          viewRangeRef.current = [newX0, newX1];
          Plotly.relayout(el, {
            "xaxis.range": [newX0, newX1],
            "xaxis.autorange": false,
          });
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

  // Drain pending buffers into raw storage (called via RAF from useWebSocket)
  const flushToPlot = useCallback(() => {
    rafRef.current = null;
    if (pendingX.current.length === 0) return;

    const px = pendingX.current;
    const py = pendingY.current;
    pendingX.current = [];
    pendingY.current = [];

    const rx = rawX.current;
    const ry = rawY.current;
    for (let i = 0; i < px.length; i++) {
      rx.push(px[i]);
      ry.push(py[i]);
    }
  }, []);

  // Periodic render: trim raw buffer, downsample via LTTB, push to Plotly.
  useEffect(() => {
    const interval = setInterval(() => {
      setSampleCount(indexRef.current);
      if (!plotRef.current || !plotInited.current) return;
      if (rawX.current.length === 0) return;

      // Skip render while user is actively dragging (hard block)
      if (isDraggingRef.current) return;

      // Skip heavy Plotly.react() while interaction cooldown is active
      if (performance.now() < interactingUntil.current) return;

      // Trim oldest samples when raw buffer exceeds capacity
      if (rawX.current.length > MAX_RAW_POINTS) {
        const excess = rawX.current.length - MAX_RAW_POINTS;
        rawX.current = rawX.current.slice(excess);
        rawY.current = rawY.current.slice(excess);
      }

      // Downsample for display
      let displayX, displayY;
      if (rawX.current.length <= MAX_RENDER_POINTS) {
        displayX = rawX.current;
        displayY = rawY.current;
      } else {
        const ds = lttbDownsample(
          rawX.current,
          rawY.current,
          MAX_RENDER_POINTS,
        );
        displayX = ds.x;
        displayY = ds.y;
      }

      // Compute y-range from raw data within the visible x-window (stable, no LTTB variance)
      const rx = rawX.current,
        ry = rawY.current;
      let visX0, visX1;
      if (isFollowingRef.current && rx.length > 0) {
        visX1 = rx[rx.length - 1];
        visX0 = Math.max(0, visX1 - VIEW_WINDOW_S);
      } else {
        const r = plotRef.current._fullLayout?.xaxis?.range;
        visX0 = r?.[0];
        visX1 = r?.[1];
      }
      let yMin = Infinity,
        yMax = -Infinity;
      let lo = 0,
        hi = rx.length - 1;
      if (visX0 != null && visX1 != null) {
        let a = 0,
          b = rx.length - 1;
        while (a < b) {
          const m = (a + b) >> 1;
          if (rx[m] < visX0) a = m + 1;
          else b = m;
        }
        lo = a;
        a = lo;
        b = rx.length - 1;
        while (a < b) {
          const m = (a + b + 1) >> 1;
          if (rx[m] > visX1) b = m - 1;
          else a = m;
        }
        hi = a;
      }
      for (let i = lo; i <= hi; i++) {
        if (ry[i] < yMin) yMin = ry[i];
        if (ry[i] > yMax) yMax = ry[i];
      }
      const yPad = yMin === yMax ? 1 : (yMax - yMin) * 0.08;
      const yRangeComputed = isFinite(yMin) ? [yMin - yPad, yMax + yPad] : null;

      Plotly.react(
        plotRef.current,
        [{ x: displayX, y: displayY, ...traceTemplate.current }],
        (() => {
          const layout = { ...layoutRef.current, dragmode: false };
          layout.yaxis = {
            ...layout.yaxis,
            autorange: yRangeComputed ? false : true,
            range: yRangeComputed ?? undefined,
            fixedrange: true,
          };
          if (isFollowingRef.current && rawX.current.length > 0) {
            const latestT = rawX.current[rawX.current.length - 1];
            const liveRange = [Math.max(0, latestT - VIEW_WINDOW_S), latestT];
            viewRangeRef.current = liveRange;
            layout.xaxis = {
              ...layout.xaxis,
              range: liveRange,
              autorange: false,
            };
          } else if (viewRangeRef.current) {
            layout.xaxis = {
              ...layout.xaxis,
              range: viewRangeRef.current,
              autorange: false,
            };
          }
          return layout;
        })(),
      );
    }, RENDER_INTERVAL_MS);

    return () => clearInterval(interval);
  }, []);

  const resetPlot = useCallback(() => {
    indexRef.current = 0;
    rawX.current = [];
    rawY.current = [];
    pendingX.current = [];
    pendingY.current = [];
    isFollowingRef.current = true;
    setIsFollowing(true);
    setSampleCount(0);
    if (plotRef.current) {
      Plotly.react(
        plotRef.current,
        [{ x: [], y: [], ...traceTemplate.current }],
        layoutRef.current,
      );
    }
  }, []);

  const jumpToLive = useCallback(() => {
    isFollowingRef.current = true;
    setIsFollowing(true);
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
    isFollowing,
    jumpToLive,
  };
}
