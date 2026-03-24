"""
ADC WebSocket + Static File Server
------------------------------------
Serves a React app (from ./dist) AND streams 16-bit ADC sample data.
  GET  /*         → serves React app (../dist/)
  WS   /ws        → ADC binary stream (send "start" / "stop")

Install:  pip install aiohttp
Run:      python ws_server.py
"""

import asyncio
import math
import random
import struct
import logging
from pathlib import Path
from aiohttp import web

logging.getLogger("aiohttp").setLevel(logging.ERROR)

# Tuning knobs
SAMPLES_PER_BATCH = 200   # samples per WebSocket frame
SEND_INTERVAL     = 0.05  # seconds between frames  (50 ms → 4 kSa/s effective)
ADC_MID           = 32768 # midpoint of 16-bit range
NOISE_AMP         = 200   # gaussian noise std-dev
VIRTUAL_RATE      = 4000  # virtual sample-rate used for time calculation
DIST_DIR          = Path(__file__).parent / "../dist"   # React build output



def generate_adc_batch(t_offset: float, n: int) -> bytes:
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
    return struct.pack(f'<{n}H', *samples)


# ── WebSocket handler ─────────────────────────────────────────────────────────
async def ws_handler(request):
    ws = web.WebSocketResponse()
    await ws.prepare(request)

    streaming      = False
    task           = None
    sample_counter = 0

    async def stream_loop():
        nonlocal sample_counter
        try:
            loop      = asyncio.get_event_loop()
            next_send = loop.time()
            while True:
                t_offset = sample_counter / VIRTUAL_RATE
                batch    = generate_adc_batch(t_offset, SAMPLES_PER_BATCH)
                await ws.send_bytes(batch)
                sample_counter += SAMPLES_PER_BATCH
                next_send      += SEND_INTERVAL
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
                streaming      = True
                sample_counter = 0
                task           = asyncio.create_task(stream_loop())
                print("[▶] Streaming started")

            elif cmd == "stop" and streaming:
                streaming = False
                if task:
                    task.cancel()
                    await task
                    task = None
                print("[■] Streaming stopped")

    except Exception as e:
        print(f"[!] Error: {e}")
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
    """
    Serve files from dist/.  Fall back to index.html for any path
    that doesn't match a real file (supports client-side routing).
    """
    rel_path = request.match_info.get("path", "")
    target   = DIST_DIR / rel_path

    # Resolve to an actual file (handle bare directory → index.html)
    if target.is_dir():
        target = target / "index.html"

    if target.is_file():
        return web.FileResponse(target)

    # SPA fallback: any unknown route → index.html
    index = DIST_DIR / "index.html"
    if index.is_file():
        return web.FileResponse(index)

    raise web.HTTPNotFound(text="dist/ not found. Did you run `npm run build`?")


# ── App factory ───────────────────────────────────────────────────────────────
def make_app() -> web.Application:
    app = web.Application()
    app.router.add_get("/ws", ws_handler)
    app.router.add_get("/",              static_handler)   # root
    app.router.add_get("/{path:.+}",     static_handler)   # everything else
    return app


async def main():
    host, port = "0.0.0.0", 8765
    app        = make_app()
    runner     = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, host, port)
    await site.start()
    print(f"Server running on http://{host}:{port}")
    print(f"  React app  →  http://localhost:{port}/")
    print(f"  WebSocket  →  ws://localhost:{port}/ws")
    if not DIST_DIR.is_dir():
        print(f"  ⚠  '{DIST_DIR}' not found - run `npm run build` first")
    await asyncio.Event().wait()   # run forever


if __name__ == "__main__":
    asyncio.run(main())