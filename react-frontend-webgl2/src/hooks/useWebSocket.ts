import { useState, useEffect, useRef, useCallback } from "react";
import { WS_URL } from "../config/constants";

type ConnectionStatus = "connected" | "disconnected" | "error";

interface UseWebSocketParams {
  url?: string;
  onData: (chunk: Uint16Array) => void;
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
        onDataRef.current?.(new Uint16Array(e.data));
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
