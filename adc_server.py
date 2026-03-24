"""
ADC WebSocket + Static File Server
------------------------------------
Reads real ADC data from a TCP stream (rpi_data_buff_extract) and forwards
it to the React frontend via WebSocket as binary Uint16 frames.
Falls back to simulated data when the ADC stream is unavailable.

  GET  /*         -> serves React app (react-frontend/dist/)
  WS   /ws        -> ADC binary stream (send "start" / "stop")

Environment variables:
  ADC_STREAM_HOST     TCP host of rpi_data_buff_extract  (default: 127.0.0.1)
  ADC_STREAM_PORT     TCP port of rpi_data_buff_extract  (default: 9000)
  ADC_SERVER_PORT     Port this server listens on         (default: 8765)
  ADC_USE_SIM         Set to "1" to force simulation mode

Install:  pip install aiohttp
Run:      python adc_server.py
"""

import asyncio
import math
import os
import random
import struct
import logging
from pathlib import Path
from aiohttp import web

logging.getLogger("aiohttp").setLevel(logging.ERROR)

# ── Configuration ─────────────────────────────────────────────────────────────
ADC_STREAM_HOST = os.environ.get("ADC_STREAM_HOST", "127.0.0.1")
ADC_STREAM_PORT = int(os.environ.get("ADC_STREAM_PORT", "9000"))
SERVER_PORT     = int(os.environ.get("ADC_SERVER_PORT", "8765"))
USE_SIM         = os.environ.get("ADC_USE_SIM", "").lower() in ("1", "true", "yes")

# Simulation parameters (match ws_server.py defaults)
SIM_SAMPLES_PER_BATCH = 200
SIM_SEND_INTERVAL     = 0.05   # 50 ms
ADC_MID               = 32768
NOISE_AMP             = 200
VIRTUAL_RATE          = 4000

RECONNECT_DELAY = 1.0          # seconds between TCP reconnect attempts
CONNECT_TIMEOUT = 5.0          # seconds for TCP connect timeout

DIST_DIR = Path(__file__).resolve().parent / "react-frontend" / "dist"


# ── CSV-to-binary conversion ─────────────────────────────────────────────────
def csv_to_binary(csv_line: str) -> bytes | None:
    """Parse a CSV line of integer ADC values into little-endian Uint16 bytes."""
    try:
        values = [max(0, min(65535, int(v)))
                  for v in csv_line.split(",") if v.strip()]
        if not values:
            return None
        return struct.pack(f"<{len(values)}H", *values)
    except (ValueError, struct.error):
        return None


# ── Simulation data generator ─────────────────────────────────────────────────
def generate_sim_batch(t_offset: float, n: int) -> bytes:
    samples = []
    for i in range(n):
        t = t_offset + i / VIRTUAL_RATE
        drift    = 5000 * math.sin(2 * math.pi * 0.05 * t)
        periodic = 8000 * math.sin(2 * math.pi * 1.2  * t)
        harmonic = 2000 * math.sin(2 * math.pi * 3.6  * t)
        noise    = random.gauss(0, NOISE_AMP)
        value    = int(ADC_MID + drift + periodic + harmonic + noise)
        value    = max(0, min(65535, value))
        samples.append(value)
    return struct.pack(f"<{n}H", *samples)


# ── WebSocket handler ─────────────────────────────────────────────────────────
async def ws_handler(request):
    ws = web.WebSocketResponse()
    await ws.prepare(request)

    streaming = False
    task      = None

    async def stream_from_adc():
        """Connect to the ADC TCP stream and forward data as binary WS frames."""
        reader = writer = None
        try:
            while True:
                # (Re)connect loop
                if reader is None:
                    try:
                        reader, writer = await asyncio.wait_for(
                            asyncio.open_connection(ADC_STREAM_HOST, ADC_STREAM_PORT),
                            timeout=CONNECT_TIMEOUT,
                        )
                        print(f"[+] Connected to ADC stream {ADC_STREAM_HOST}:{ADC_STREAM_PORT}")
                    except (OSError, asyncio.TimeoutError) as exc:
                        print(f"[!] ADC connect failed ({exc}), retrying in {RECONNECT_DELAY}s")
                        await asyncio.sleep(RECONNECT_DELAY)
                        continue

                # Read one CSV line from the ADC TCP stream
                try:
                    raw_line = await asyncio.wait_for(reader.readline(), timeout=2.0)
                except asyncio.TimeoutError:
                    continue  # no data yet, keep reading

                if not raw_line:
                    print("[!] ADC stream closed by peer, reconnecting")
                    if writer:
                        writer.close()
                    reader = writer = None
                    await asyncio.sleep(RECONNECT_DELAY)
                    continue

                data = csv_to_binary(raw_line.decode("ascii", errors="replace").strip())
                if data:
                    await ws.send_bytes(data)

        except asyncio.CancelledError:
            pass
        finally:
            if writer:
                writer.close()

    async def stream_sim():
        """Generate simulated ADC data (same waveform as ws_server.py)."""
        sample_counter = 0
        try:
            loop      = asyncio.get_event_loop()
            next_send = loop.time()
            while True:
                t_offset = sample_counter / VIRTUAL_RATE
                batch    = generate_sim_batch(t_offset, SIM_SAMPLES_PER_BATCH)
                await ws.send_bytes(batch)
                sample_counter += SIM_SAMPLES_PER_BATCH
                next_send      += SIM_SEND_INTERVAL
                delay = next_send - loop.time()
                if delay > 0:
                    await asyncio.sleep(delay)
        except asyncio.CancelledError:
            pass

    try:
        async for msg in ws:
            from aiohttp import WSMsgType
            if msg.type != WSMsgType.TEXT:
                continue

            cmd = msg.data.strip().lower()

            if cmd == "start" and not streaming:
                streaming = True
                if USE_SIM:
                    task = asyncio.create_task(stream_sim())
                    print("[>] Streaming started (simulation)")
                else:
                    task = asyncio.create_task(stream_from_adc())
                    print("[>] Streaming started (ADC)")

            elif cmd == "stop" and streaming:
                streaming = False
                if task:
                    task.cancel()
                    await task
                    task = None
                print("[x] Streaming stopped")

    except Exception as exc:
        print(f"[!] WS error: {exc}")
    finally:
        if task:
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass
        print("[-] Client disconnected")

    return ws


# ── Static file / SPA handler ─────────────────────────────────────────────────
async def static_handler(request):
    rel_path = request.match_info.get("path", "")
    target   = DIST_DIR / rel_path

    if target.is_dir():
        target = target / "index.html"

    if target.is_file():
        return web.FileResponse(target)

    index = DIST_DIR / "index.html"
    if index.is_file():
        return web.FileResponse(index)

    raise web.HTTPNotFound(text="dist/ not found. Did you run `npm run build`?")


# ── App factory ───────────────────────────────────────────────────────────────
def make_app() -> web.Application:
    app = web.Application()
    app.router.add_get("/ws", ws_handler)
    app.router.add_get("/",          static_handler)
    app.router.add_get("/{path:.+}", static_handler)
    return app


async def main():
    host, port = "0.0.0.0", SERVER_PORT
    app    = make_app()
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, host, port)
    await site.start()

    mode = "SIMULATION" if USE_SIM else f"ADC stream -> {ADC_STREAM_HOST}:{ADC_STREAM_PORT}"
    print(f"ADC server running on http://{host}:{port}")
    print(f"  Mode       ->  {mode}")
    print(f"  React app  ->  http://localhost:{port}/")
    print(f"  WebSocket  ->  ws://localhost:{port}/ws")
    if not DIST_DIR.is_dir():
        print(f"  Warning: '{DIST_DIR}' not found - run `npm run build` first")

    await asyncio.Event().wait()


if __name__ == "__main__":
    asyncio.run(main())
