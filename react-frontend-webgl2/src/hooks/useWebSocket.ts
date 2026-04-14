import { useState, useEffect, useRef, useCallback } from "react";
import { WS_URL, MAX_SAMPS, CHUNK_BYTES } from "../config/constants";

type ConnectionStatus = "connected" | "disconnected" | "error";

/**
 * Decoded result from one WebSocket binary frame.
 */
export interface ParsedFrame {
  samples: Float32Array;
  chunkUsecs: number[];
}

/**
 * Decode one WebSocket binary frame containing one or more mvaring chunks.
 *
 * Wire format per chunk (little-endian):
 *   [usecs: uint32][samples: MAX_SAMPS × uint32][gpio_lev0: MAX_SAMPS × uint32]
 *
 * ADC extraction mirrors the C macro ADC_RAW_VAL:
 *   byte_swap16(raw & 0xFFFF) & 0x7FF  →  11-bit value (0..2047)
 */
function parseAdcFrame(buf: ArrayBuffer): ParsedFrame {
  const numChunks = Math.floor(buf.byteLength / CHUNK_BYTES);
  if (numChunks === 0) return { samples: new Float32Array(0), chunkUsecs: [] };

  const samples = new Float32Array(numChunks * MAX_SAMPS);
  const chunkUsecs: number[] = [];
  const dv = new DataView(buf);
  let outIdx = 0;

  for (let c = 0; c < numChunks; c++) {
    const chunkBase = c * CHUNK_BYTES;
    chunkUsecs.push(dv.getUint32(chunkBase, true));
    const samplesOffset = chunkBase + 4; // skip usecs
    for (let i = 0; i < MAX_SAMPS; i++) {
      const raw32 = dv.getUint32(samplesOffset + i * 4, true);
      const raw16 = raw32 & 0xffff;
      // byte-swap 16-bit then mask to 11 bits
      const swapped = ((raw16 & 0xff) << 8) | ((raw16 >> 8) & 0xff);
      samples[outIdx++] = swapped & 0x7ff;
    }
  }

  return { samples: samples.subarray(0, outIdx), chunkUsecs };
}

interface UseWebSocketParams {
  url?: string;
  onData: (frame: ParsedFrame) => void;
}

interface UseWebSocketReturn {
  status: ConnectionStatus;
  streaming: boolean;
  sendCommand: (cmd: string) => void;
}

const RECONNECT_INTERVAL_MS = 2000;

export function useWebSocket({
  url = WS_URL,
  onData,
}: UseWebSocketParams): UseWebSocketReturn {
  const [status, setStatus] = useState<ConnectionStatus>("disconnected");
  const [streaming, setStreaming] = useState(false);

  const wsRef = useRef<WebSocket | null>(null);
  const reconnectTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  // Use a ref so onData changes never trigger WS reconnection
  const onDataRef = useRef(onData);
  useEffect(() => {
    onDataRef.current = onData;
  }, [onData]);

  useEffect(() => {
    let cancelled = false;

    function connect() {
      if (cancelled) return;

      const ws = new WebSocket(url);
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
        reconnectTimerRef.current = setTimeout(connect, RECONNECT_INTERVAL_MS);
      };

      ws.onerror = () => {
        if (cancelled) return;
        setStatus("error");
      };

      ws.onmessage = (e: MessageEvent) => {
        if (cancelled) return;
        if (!(e.data instanceof ArrayBuffer)) return;
        const frame = parseAdcFrame(e.data);
        if (frame.samples.length > 0) {
          // DEBUG: mirror the Python [adc] log for comparison
          const N_PREVIEW = 8;
          const usecs = frame.chunkUsecs[0] ?? 0;
          const adcParts = Array.from(
            { length: N_PREVIEW },
            (_, i) => `[${i}]=${frame.samples[i]}`,
          );
          console.log(
            `[adc] chunks=${frame.chunkUsecs.length} usecs=${usecs} ${adcParts.join(" ")} bytes=${e.data.byteLength}`,
          );
          onDataRef.current?.(frame);
        }
      };
    }

    reconnectTimerRef.current = setTimeout(connect, 50);

    return () => {
      cancelled = true;
      if (reconnectTimerRef.current) clearTimeout(reconnectTimerRef.current);
      wsRef.current?.close();
    };
  }, [url]);

  const sendCommand = useCallback((cmd: string) => {
    if (wsRef.current?.readyState !== WebSocket.OPEN) return;
    wsRef.current.send(cmd);
    if (cmd === "start") {
      setStreaming(true);
    } else {
      setStreaming(false);
    }
  }, []);

  return { status, streaming, sendCommand };
}
