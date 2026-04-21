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

  ADC value extraction: byte_swap16(sample & 0xFFFF), masked client-side
    based on the selected ADC bit depth (e.g. & 0x7FF for 11-bit, & 0xFFF for 12-bit)

Environment variables:
  ADC_SERVER_PORT     Port this server listens on   (default: 8765)
  ADC_USE_SIM         Set to "1" to force simulation mode

Install:  pip install aiohttp
Run:      python adc_server.py
"""

import asyncio
import json
import math
import os
import random
import struct
import sys
import logging
from pathlib import Path
from aiohttp import web

logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s %(levelname)-8s %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("adc_server")
logging.getLogger("aiohttp").setLevel(logging.ERROR)

try:
    import mvaring
    _MVARING_AVAILABLE = True
except ImportError:
    _MVARING_AVAILABLE = False

SHM_NAME = "/RPI_ADC_BUFF"
# Match struct adc_data in mvaring.h; fall back to the common.h default if
# the compiled extension is not present (e.g. running on a dev machine).
MAX_SAMPS = mvaring.MAX_SAMPS if _MVARING_AVAILABLE else 1024

# ── Configuration ─────────────────────────────────────────────────────────────
SERVER_PORT = int(os.environ.get("ADC_SERVER_PORT", "80"))
# Force simulation if the env variable is set OR if mvaring is not available.
USE_SIM     = (os.environ.get("ADC_USE_SIM", "").lower() in ("1", "true", "yes")
               or not _MVARING_AVAILABLE)

# Number of ring buffer chunks to read per poll.
# 16 chunks × 1024 samples = 16 384 samples ≈ 16 ms at 1 MSPS.
RING_READ_CHUNKS = 16

# Simulation parameters
# Waveform amplitudes are scaled for the 12-bit ADC range (0..4095).
VIRTUAL_RATE = 4000   # simulated samples/sec
ADC_MID      = 2048   # midpoint of 12-bit range
NOISE_AMP    = 12     # gaussian noise amplitude (12-bit scale)

DIST_DIR = Path(__file__).resolve().parent / "react-frontend" / "dist"

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
STREAMER_PROGRAM = os.path.join(BASE_DIR, "rpi_adc_stream")


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
    """Encode a 12-bit ADC value into the raw SPI word format used by MCP3202.

    This is the inverse of the C macro:
        ADC_RAW_VAL(d) = (((uint16_t)(d)<<8 | (uint16_t)(d)>>8) & 0xfff)

    So encode_adc_val(v) produces a raw word d such that ADC_RAW_VAL(d) == v.
    """
    val &= 0xFFF  # clamp to 12 bits
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
        value    = max(0, min(4095, value))  # clamp to 12-bit range
        samples.append(encode_adc_val(value))
    return (struct.pack("<I", usecs)
            + struct.pack(f"<{MAX_SAMPS}I", *samples)
            + bytes(MAX_SAMPS * 4))  # gpio_lev0 zeroed for simulation


# ── WebSocket handler ─────────────────────────────────────────────────────────
async def ws_handler(request):
    ws = web.WebSocketResponse()
    await ws.prepare(request)

    streaming      = False
    task           = None
    streamer_proc  = None
    stderr_task    = None
    watcher_task   = None
    
    
    async def drain_stdout(proc):
        """Drain rpi_adc_stream stdout; send actual sample rate to frontend when seen."""
        try:
            async for line in proc.stdout:
                text = line.decode(errors="replace").rstrip()
                if text.startswith("Actual sample-rate:"):
                    _, _, val = text.partition(": ")
                    try:
                        rate = int(val)
                        log.info("Actual sample-rate: %d Hz", rate)
                        await ws.send_str(json.dumps({"type": "actual_sample_rate", "value": rate}))
                    except ValueError:
                        log.info("Actual sample-rate: %s Hz", val)
        except asyncio.CancelledError:
            pass

    async def log_stderr(proc):
        """Forward rpi_adc_stream stderr lines to web server logger."""
        try:
            async for line in proc.stderr:
                text = line.decode(errors="replace").rstrip()
                if text:
                    log.warning("[rpi_adc_stream] %s", text)
        except asyncio.CancelledError:
            pass

    async def kill_streamer(proc, stderr_log_task, watch_task):
        """Send SIGINT for graceful DMA shutdown, wait, then SIGKILL if needed."""
        if watch_task:
            watch_task.cancel()
        if proc is None or proc.returncode is not None:
            if stderr_log_task:
                stderr_log_task.cancel()
            return
        try:
            import signal
            proc.send_signal(signal.SIGINT)   # graceful: lets rpi_adc_stream release DMA
            await asyncio.wait_for(proc.wait(), timeout=5.0)
        except asyncio.TimeoutError:
            proc.kill()
            await proc.wait()
        if stderr_log_task:
            await asyncio.gather(stderr_log_task, return_exceptions=True)
        if watch_task:
            await asyncio.gather(watch_task, return_exceptions=True)
        log.info("rpi_adc_stream process stopped (pid %d, rc %d)", proc.pid, proc.returncode)

    async def watch_streamer(proc):
        """Log a warning if rpi_adc_stream exits unexpectedly."""
        try:
            rc = await proc.wait()
            log.warning("rpi_adc_stream exited unexpectedly (pid %d, rc %d)", proc.pid, rc)
        except asyncio.CancelledError:
            pass

    async def stream_from_adc(proc):
        """Read from the mvaring ring buffer and forward data as binary WS frames."""
        # Wait for rpi_adc_stream to initialise the ring buffer.
        # Poll until the ring is available (up to 3 s).
        deadline = asyncio.get_event_loop().time() + 3.0
        handle = None
        while asyncio.get_event_loop().time() < deadline:
            if proc.returncode is not None:
                log.error("rpi_adc_stream exited (rc %d) before ring was ready", proc.returncode)
                return
            try:
                handle = mvaring.open(SHM_NAME)
                break
            except (OSError, ValueError):
                await asyncio.sleep(0.1)
        if handle is None:
            try:
                handle = mvaring.open(SHM_NAME)
            except OSError as e:
                log.error("Cannot open shared memory '%s': %s", SHM_NAME, e)
                return
            except ValueError as e:
                log.error("Ring buffer invalid: %s", e)
                return

        log.info("Opened ring buffer '%s'", SHM_NAME)
        empty_log_counter = 0
        try:
            while True:
                chunks = mvaring.read(handle, RING_READ_CHUNKS)
                if not chunks:
                    await asyncio.sleep(0.001)  # ring empty — yield to event loop
                    empty_log_counter += 1
                    if empty_log_counter % 5000 == 0:  # log every ~5 s
                        log.warning("Ring buffer still empty after %d polls (no data from rpi_adc_stream)",
                                    empty_log_counter)
                    continue
                empty_log_counter = 0
                data = chunks_to_bytes(chunks)
                # DEBUG: log first chunk's usecs and a few raw (byte-swapped) SPI words
                usecs_val = struct.unpack_from("<I", data, 0)[0]
                n_preview = 8
                adc_vals = []
                for i in range(n_preview):
                    raw = struct.unpack_from("<I", data, 4 + i * 4)[0]
                    raw16 = raw & 0xFFFF
                    swapped = ((raw16 & 0xFF) << 8 | (raw16 >> 8) & 0xFF) & 0xFFFF
                    adc_vals.append(swapped)
                adc_str = " ".join(f"[{i}]={v}" for i, v in enumerate(adc_vals))
                # log.debug("chunks=%d usecs=%d %s bytes=%d",
                #           len(chunks), usecs_val, adc_str, len(data))

                await ws.send_bytes(data)
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

            cmd = msg.data.strip()
            parts = cmd.split()
            cmd_name = parts[0].lower()

            if cmd_name == "start" and not streaming:
                sample_rate = None
                if len(parts) > 1:
                    try:
                        sample_rate = int(parts[1]) // 1000  # frontend sends Hz; -r expects kHz
                    except ValueError:
                        log.warning("Invalid sample rate '%s', ignoring", parts[1])
                streaming = True
                if USE_SIM:
                    task = asyncio.create_task(stream_sim())
                    log.info("Streaming started (simulation)")
                else:
                    streamer_cmd = ["sudo", "stdbuf", "-o0", STREAMER_PROGRAM, "-c"]
                    if sample_rate is not None:
                        streamer_cmd += ["-r", str(sample_rate)]
                    streamer_proc = await asyncio.create_subprocess_exec(
                        *streamer_cmd,
                        stdout=asyncio.subprocess.PIPE,
                        stderr=asyncio.subprocess.PIPE,
                    )
                    log.info("Started rpi_adc_stream (pid %d)%s", streamer_proc.pid,
                             f" at {sample_rate} kHz" if sample_rate else "")

                    # Now start tasks — log_stderr will pick up from where header reading left off
                    stderr_task = asyncio.create_task(log_stderr(streamer_proc))
                    watcher_task = asyncio.create_task(watch_streamer(streamer_proc))
                    asyncio.create_task(drain_stdout(streamer_proc))
                    task = asyncio.create_task(stream_from_adc(streamer_proc))
                    log.info("Streaming started (ADC)")

            elif cmd_name == "stop" and streaming:
                streaming = False
                if task:
                    task.cancel()
                    await task
                    task = None
                await kill_streamer(streamer_proc, stderr_task, watcher_task)
                streamer_proc = None
                stderr_task = None
                watcher_task = None
                log.info("Streaming stopped")

    except Exception as exc:
        log.error("WS error: %s", exc)
    finally:
        if task:
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass
        await kill_streamer(streamer_proc, stderr_task, watcher_task)
        log.info("Client disconnected")

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
    log.info("ADC server running on http://%s:%d", host, port)
    log.info("  Mode       ->  %s", mode)
    log.info("  React app  ->  http://localhost:%d/", port)
    log.info("  WebSocket  ->  ws://localhost:%d/ws", port)
    if not DIST_DIR.is_dir():
        log.warning("'%s' not found - run `npm run build` first", DIST_DIR)

    await asyncio.Event().wait()


if __name__ == "__main__":
    asyncio.run(main())
