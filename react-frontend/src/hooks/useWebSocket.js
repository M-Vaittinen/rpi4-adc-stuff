import { useState, useEffect, useRef, useCallback } from "react";
import { WS_URL } from "../config/constants";

const RECONNECT_INTERVAL_MS = 2000;

/**
 * @param {Object} opts
 * @param {number} opts.adcBits
 * @param {number} opts.sampleRate
 * @param {Object|null} opts.slot - { indexRef, pendingX, pendingY, rafRef, flushToPlot, resetPlot }
 */
export function useWebSocket({
  adcBits,
  sampleRate,
  slot,
}) {
  const [status, setStatus] = useState("disconnected");
  const [streaming, setStreaming] = useState(false);

  const wsRef = useRef(null);
  const adcBitsRef = useRef(adcBits);
  const sampleRateRef = useRef(sampleRate);
  const slotRef = useRef(slot);
  const reconnectTimerRef = useRef(null);

  useEffect(() => { adcBitsRef.current = adcBits; }, [adcBits]);
  useEffect(() => { sampleRateRef.current = sampleRate; }, [sampleRate]);
  useEffect(() => { slotRef.current = slot; }, [slot]);

  useEffect(() => {
    let cancelled = false;

    function connect() {
      if (cancelled) return;

      const ws = new WebSocket(WS_URL);
      ws.binaryType = "arraybuffer";
      wsRef.current = ws;

      ws.onopen = () => {
        if (cancelled) return;
        setStatus("connected");
      };

      ws.onclose = () => {
        if (cancelled) return;
        setStatus("disconnected");
        setStreaming(false);
        console.log("trying to reconnect...");
        reconnectTimerRef.current = setTimeout(connect, RECONNECT_INTERVAL_MS);
      };

      ws.onerror = () => {
        if (cancelled) return;
        setStatus("error");
      };

      ws.onmessage = (e) => {
        if (cancelled) return;
        if (!(e.data instanceof ArrayBuffer)) return;

        const slot = slotRef.current;
        if (!slot) return;

        const raw = new Uint16Array(e.data);
        const maxAdc = adcBitsRef.current === 16 ? 65535 : 4095;
        const rate = sampleRateRef.current;

        const startIdx = slot.indexRef.current;
        let count = 0;
        for (let i = 0; i < raw.length; i++) {
          const t = (startIdx + count) / rate;
          const clamped = raw[i] > maxAdc ? maxAdc : raw[i];
          slot.pendingX.current.push(t);
          slot.pendingY.current.push(clamped);
          count++;
        }
        slot.indexRef.current = startIdx + count;

        if (!slot.rafRef.current) {
          slot.rafRef.current = requestAnimationFrame(slot.flushToPlot);
        }
      };
    }

    reconnectTimerRef.current = setTimeout(connect, 50);

    return () => {
      cancelled = true;
      clearTimeout(reconnectTimerRef.current);
      if (slotRef.current?.rafRef.current)
        cancelAnimationFrame(slotRef.current.rafRef.current);
      wsRef.current?.close();
    };
  }, []);

  const sendCommand = useCallback((cmd) => {
    if (wsRef.current?.readyState !== WebSocket.OPEN) return;
    wsRef.current.send(cmd);
    if (cmd === "start") {
      slotRef.current?.resetPlot();
      setStreaming(true);
    } else {
      setStreaming(false);
    }
  }, []);

  return { status, streaming, sendCommand };
}
