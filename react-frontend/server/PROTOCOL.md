# WebSocket Server Protocol

## Overview

The ADC WebSocket server streams sample data to the frontend for real-time plotting. The client controls streaming with text commands, and the server responds with binary data frames.

## Connection

- **URL:** `ws://<host>:8765`
- **Origin:** any (no origin restriction)

## Commands (client → server)

The client sends plain text messages:

| Command | Description |
|---------|-------------|
| `start` | Begin streaming. Resets the sample counter to 0. |
| `stop`  | Stop streaming. |

## Data Frames (server → client)

Each data frame is a **binary WebSocket message** containing little-endian unsigned 16-bit integers (`Uint16`, `<H` in Python struct notation).

```
Byte layout (little-endian):
[sample0_lo][sample0_hi][sample1_lo][sample1_hi]...
```

- **Batch size:** 200 samples per frame (400 bytes)
- **Send interval:** 50 ms (20 frames/sec)
- **Effective sample rate:** 4,000 Sa/s
- **Value range:** 0–65535 (unsigned 16-bit)

### Decoding in JavaScript

```js
ws.binaryType = "arraybuffer";
ws.onmessage = (e) => {
  const samples = new Uint16Array(e.data); // zero-copy view
  // samples.length === 200, each value 0–65535
};
```

### Encoding in Python

```python
import struct
data = struct.pack(f'<{n}H', *sample_list)  # n unsigned 16-bit LE
await websocket.send(data)                    # sends as binary frame
```

## Timing

The server uses absolute scheduling to prevent cumulative drift:

```
next_send = now
loop:
    generate + send batch
    next_send += SEND_INTERVAL
    sleep(next_send - now)
```

This ensures the sample rate stays accurate over long streaming sessions regardless of how long generation and sending take.

## Adapting for Real Hardware

To replace the dummy server with real ADC hardware, you need to:

1. **Match the binary format** — pack your ADC readings as little-endian `Uint16` values
2. **Set `SAMPLES_PER_BATCH` and `SEND_INTERVAL`** to match your hardware's sample rate
3. **Update `VIRTUAL_RATE`** and the frontend's sample rate setting to match

The frontend expects `ArrayBuffer` messages containing raw `Uint16` values. It handles the conversion to voltage using the reference voltage and ADC bit depth configured in the UI settings.
