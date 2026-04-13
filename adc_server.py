"""
ADC WebSocket + Static File Server
------------------------------------
Reads real ADC data directly from the mvaring shared-memory ring buffer and
forwards it to the React frontend via WebSocket as raw binary frames.
Falls back to simulated data when the ring buffer is unavailable.

  GET  /*         -> serves React app (react-frontend/dist/)
  WS   /ws        -> ADC binary stream (send "start" / "stop")

Binary wire format (WebSocket binary frames):
  One or more concatenated mvaring chunks, each:
    [usecs:    uint32 LE ]          -- microsecond timestamp
    [samples:  MAX_SAMPS × uint32 LE] -- raw SPI words; decode with ADC_RAW_VAL()
    [gpio_lev0:MAX_SAMPS × uint32 LE] -- GPIO level snapshot per sample

  ADC value extraction (mirrors the C macro ADC_RAW_VAL):
    adc_val = byte_swap16(sample & 0xFFFF) & 0x7FF   (11-bit, 0..2047)

Environment variables:
  ADC_SERVER_PORT     Port this server listens on   (default: 8765)
  ADC_USE_SIM         Set to "1" to force simulation mode

Install:  pip install aiohttp
Run:      python adc_server.py
"""

import asyncio
import math
import os
import random
import struct
import sys
import logging
from pathlib import Path
from aiohttp import web

try:
    import mvaring
    _MVARING_AVAILABLE = True
except ImportError:
    _MVARING_AVAILABLE = False

SHM_NAME = "/RPI_ADC_BUFF"
# Match struct adc_data in mvaring.h; fall back to the common.h default if
# the compiled extension is not present (e.g. running on a dev machine).
MAX_SAMPS = mvaring.MAX_SAMPS if _MVARING_AVAILABLE else 1024

logging.getLogger("aiohttp").setLevel(logging.ERROR)

# ── Configuration ─────────────────────────────────────────────────────────────
SERVER_PORT = int(os.environ.get("ADC_SERVER_PORT", "8765"))
# Force simulation if the env variable is set OR if mvaring is not available.
USE_SIM     = (os.environ.get("ADC_USE_SIM", "").lower() in ("1", "true", "yes")
               or not _MVARING_AVAILABLE)

# Number of ring buffer chunks to read per poll.
# 16 chunks × 1024 samples = 16 384 samples ≈ 16 ms at 1 MSPS.
RING_READ_CHUNKS = 16

# Simulation parameters
# Waveform amplitudes are scaled for the 11-bit ADC range (0..2047).
VIRTUAL_RATE = 4000   # simulated samples/sec
ADC_MID      = 1024   # midpoint of 11-bit range
NOISE_AMP    = 6      # gaussian noise amplitude (11-bit scale)

DIST_DIR = Path(__file__).resolve().parent / "react-frontend" / "dist"


# ── CSV-to-binary conversion (legacy / TCP backup path) ──────────────────────
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


# ── mvaring helpers ───────────────────────────────────────────────────────────
def encode_adc_val(val: int) -> int:
    """Encode an 11-bit ADC value into the raw SPI word format used by MCP3202.

    This is the inverse of the C macro:
        ADC_RAW_VAL(d) = (((uint16_t)(d)<<8 | (uint16_t)(d)>>8) & 0x7ff)

    So encode_adc_val(v) produces a raw word d such that ADC_RAW_VAL(d) == v.
    """
    val &= 0x7FF  # clamp to 11 bits
    return ((val << 8) | (val >> 8)) & 0xFFFF


def chunks_to_bytes(chunks: list) -> bytes:
    """Serialise a list of mvaring chunk dicts into the binary wire format.

    Each chunk contributes:
        [usecs: uint32 LE][samples: MAX_SAMPS × uint32 LE][gpio_lev0: MAX_SAMPS × uint32 LE]

    The 'samples' and 'gpio_lev0' values are already raw LE bytes from the C
    struct, so they are appended without further conversion.
    """
    parts = []
    for chunk in chunks:
        parts.append(struct.pack("<I", chunk["usecs"]))
        parts.append(chunk["samples"])    # MAX_SAMPS × uint32, raw LE bytes
        parts.append(chunk["gpio_lev0"])  # MAX_SAMPS × uint32, raw LE bytes
    return b"".join(parts)


# ── Simulation data generator ─────────────────────────────────────────────────
def generate_sim_batch(t_offset: float) -> bytes:
    """Generate one simulated mvaring chunk in the same binary format as real data.

    Output binary layout (identical to struct adc_data serialised LE):
        [usecs:     uint32 LE           ]   4 bytes
        [samples:   MAX_SAMPS × uint32 LE]  MAX_SAMPS * 4 bytes
        [gpio_lev0: MAX_SAMPS × uint32 LE]  MAX_SAMPS * 4 bytes  (zeroed)

    Sample values are raw SPI words encoded with encode_adc_val(), so the
    JavaScript consumer can apply the same ADC_RAW_VAL extraction as for real
    data without any special-casing for the simulation path.
    """
    usecs = int(t_offset * 1_000_000) & 0xFFFF_FFFF
    samples = []
    for i in range(MAX_SAMPS):
        t = t_offset + i / VIRTUAL_RATE
        drift    = 150 * math.sin(2 * math.pi * 0.05 * t)
        periodic = 250 * math.sin(2 * math.pi * 1.2  * t)
        harmonic =  60 * math.sin(2 * math.pi * 3.6  * t)
        noise    = random.gauss(0, NOISE_AMP)
        value    = int(ADC_MID + drift + periodic + harmonic + noise)
        value    = max(0, min(2047, value))  # clamp to 11-bit range
        samples.append(encode_adc_val(value))
    return (struct.pack("<I", usecs)
            + struct.pack(f"<{MAX_SAMPS}I", *samples)
            + bytes(MAX_SAMPS * 4))  # gpio_lev0 zeroed for simulation


# ── WebSocket handler ─────────────────────────────────────────────────────────
async def ws_handler(request):
    ws = web.WebSocketResponse()
    await ws.prepare(request)

    streaming = False
    task      = None

    async def stream_from_adc():
        """Read from the mvaring ring buffer and forward data as binary WS frames."""
        try:
            handle = mvaring.open(SHM_NAME)
        except OSError as e:
            print(f"[!] Cannot open shared memory '{SHM_NAME}': {e}", file=sys.stderr)
            print("[!] Is rpi_adc_stream running? (sudo ./rpi_adc_stream)", file=sys.stderr)
            return
        except ValueError as e:
            print(f"[!] Ring buffer invalid: {e}", file=sys.stderr)
            return

        print(f"[+] Opened ring buffer '{SHM_NAME}'")
        try:
            while True:
                chunks = mvaring.read(handle, RING_READ_CHUNKS)
                if not chunks:
                    await asyncio.sleep(0.001)  # ring empty — yield to event loop
                    continue
                await ws.send_bytes(chunks_to_bytes(chunks))
        except asyncio.CancelledError:
            pass
        finally:
            mvaring.close(handle)

    async def stream_sim():
        """Generate simulated ADC data in the same binary format as real mvaring chunks."""
        sample_counter = 0
        try:
            loop      = asyncio.get_event_loop()
            next_send = loop.time()
            while True:
                t_offset  = sample_counter / VIRTUAL_RATE
                batch     = generate_sim_batch(t_offset)
                await ws.send_bytes(batch)
                sample_counter += MAX_SAMPS
                next_send      += MAX_SAMPS / VIRTUAL_RATE
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

    mode = "SIMULATION" if USE_SIM else f"mvaring ring buffer ({SHM_NAME})"
    print(f"ADC server running on http://{host}:{port}")
    print(f"  Mode       ->  {mode}")
    print(f"  React app  ->  http://localhost:{port}/")
    print(f"  WebSocket  ->  ws://localhost:{port}/ws")
    if not DIST_DIR.is_dir():
        print(f"  Warning: '{DIST_DIR}' not found - run `npm run build` first")

    await asyncio.Event().wait()


if __name__ == "__main__":
    asyncio.run(main())
