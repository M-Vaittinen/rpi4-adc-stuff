import { useState, useEffect, useRef, useCallback } from "react";
import { WS_URL } from "../config/constants";

/**
 * @param {Object} opts
 * @param {string} opts.yAxisMode
 * @param {number} opts.adcBits
 * @param {number} opts.refVoltage
 * @param {number} opts.sampleRate
 * @param {Array} opts.channelSlots - one entry per channel, each with
 *   { indexRef, pendingX, pendingY, rafRef, flushToPlot, resetPlot }
 */
export function useWebSocket({
  yAxisMode,
  adcBits,
  refVoltage,
  sampleRate,
  channelSlots,
}) {
  const [status, setStatus] = useState("disconnected");
  const [streaming, setStreaming] = useState(false);

  const wsRef = useRef(null);
  const yAxisModeRef = useRef(yAxisMode);
  const adcBitsRef = useRef(adcBits);
  const refVoltageRef = useRef(refVoltage);
  const sampleRateRef = useRef(sampleRate);
  const slotsRef = useRef(channelSlots);

  useEffect(() => {
    yAxisModeRef.current = yAxisMode;
  }, [yAxisMode]);
  useEffect(() => {
    adcBitsRef.current = adcBits;
  }, [adcBits]);
  useEffect(() => {
    refVoltageRef.current = refVoltage;
  }, [refVoltage]);
  useEffect(() => {
    sampleRateRef.current = sampleRate;
  }, [sampleRate]);
  useEffect(() => {
    slotsRef.current = channelSlots;
  }, [channelSlots]);

  useEffect(() => {
    let ws;
    let cancelled = false;

    const timer = setTimeout(() => {
      if (cancelled) return;
      ws = new WebSocket(WS_URL);
      ws.binaryType = "arraybuffer";
      wsRef.current = ws;

      ws.onopen = () => !cancelled && setStatus("connected");
      ws.onclose = () => {
        if (!cancelled) {
          setStatus("disconnected");
          setStreaming(false);
        }
      };
      ws.onerror = () => !cancelled && setStatus("error");

      ws.onmessage = (e) => {
        if (cancelled) return;
        if (!(e.data instanceof ArrayBuffer)) return;

        const raw = new Uint16Array(e.data);
        const slots = slotsRef.current;
        const numCh = slots.length;
        const maxAdc = adcBitsRef.current === 16 ? 65535 : 4095;
        const rate = sampleRateRef.current;
        const isVoltage = yAxisModeRef.current === "voltage";
        const vRef = refVoltageRef.current;

        // Demux interleaved samples into per-channel buffers
        for (let ch = 0; ch < numCh; ch++) {
          const slot = slots[ch];
          const startIdx = slot.indexRef.current;
          const chSamples = [];
          const chTimes = [];
          let count = 0;

          for (let i = ch; i < raw.length; i += numCh) {
            const t = (startIdx + count) / rate;
            const clamped = raw[i] > maxAdc ? maxAdc : raw[i];
            const val = isVoltage ? (clamped / maxAdc) * vRef : clamped;
            chTimes.push(t);
            chSamples.push(val);
            count++;
          }

          slot.indexRef.current = startIdx + count;
          slot.pendingX.current.push(...chTimes);
          slot.pendingY.current.push(...chSamples);

          if (!slot.rafRef.current) {
            slot.rafRef.current = requestAnimationFrame(slot.flushToPlot);
          }
        }
      };
    }, 50);

    return () => {
      cancelled = true;
      clearTimeout(timer);
      for (const slot of slotsRef.current) {
        if (slot.rafRef.current) cancelAnimationFrame(slot.rafRef.current);
      }
      if (ws) ws.close();
    };
  }, []);

  const sendCommand = useCallback((cmd) => {
    if (wsRef.current?.readyState !== WebSocket.OPEN) return;
    wsRef.current.send(cmd);
    if (cmd === "start") {
      for (const slot of slotsRef.current) {
        slot.resetPlot();
      }
      setStreaming(true);
    } else {
      setStreaming(false);
    }
  }, []);

  return { status, streaming, sendCommand };
}
