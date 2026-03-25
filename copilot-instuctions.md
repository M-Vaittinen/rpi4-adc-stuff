# Copilot Instructions for RPi ADC Streaming

This project implements high-speed ADC (Analog-to-Digital Converter) data acquisition on Raspberry Pi 4 using DMA, with shared memory IPC and web-based visualization.

## Project Overview

**Purpose**: Stream ADC data at up to 1 MSPS (mega-samples per second) using Raspberry Pi 4's DMA engine, avoiding CPU-intensive polling.

**ADC Hardware**: 
- **Current/Legacy**: MCP3202 (12-bit, dual-channel) - existing code is written for this
- **Target**: BD7910x (single-channel variant) - new ADC being integrated
  - Same SPI clock: 20 MHz
  - Same sample rate: 1 MSPS
  - Datasheet location: `docs-not-to-git/` (do NOT commit to git)

**Key Components**:
- `rpi_adc_stream`: DMA-based ADC data acquisition → writes to shared memory ring buffer
- `rpi_adc_bufextract`: Consumer that reads from shared memory ring buffer → outputs CSV/text
- `test-ui`: OpenGL-based real-time visualization (actively maintained)
- `adc_server.py` + `webgl_graph.html`: Web-based WebGL visualization (**OBSOLETE** - not maintained, broken)

**Data Flow**: ADC → SPI → DMA → Ring Buffer (shared memory) → Consumer processes

## Development Environment

**Host PC** (where Copilot runs): 64-bit x86 Linux
**Target Platform**: Raspberry Pi 4 - 32-bit ARM Linux

**Important**: 
- Compiling on the host x86 machine may fail due to architecture-specific code
- Hardware-specific code (DMA, GPIO, SPI) must be compiled and run on the Raspberry Pi
- Project folder is shared via NFS:
  - Host path: `/home/mvaittin/nfsrp4/home/tmp/streaming`
  - RPi path: `/home/tmp/streaming`

**Test code for Raspberry Pi**: Place temporary test/validation code in `ai-gen-tests/` folder - this is for generated code that needs to run on the ARM target.

## Build & Run

### Build Commands

```bash
# Build all targets (release)
make

# Build with debug symbols
make dbg

# Clean build artifacts
make clean
```

**Build Targets**:
- `rpi_adc_stream` / `rpi_adc_stream_dbg`: Main ADC streaming daemon
- `rpi_adc_bufextract` / `rpi_adc_bufextract_dbg`: Data extraction/display tool
- `test-ui` / `test-ui_dbg`: OpenGL visualization (requires libglut, libGLEW, libGL)

### Test Commands

```bash
# Build tests
cd test && make

# Run shared memory tests
./test/test

# Run ring buffer tests
./test/ringtest
```

No automated test suite exists; tests are manual validation programs.

### Running the System

```bash
# Terminal 1: Start ADC streaming (requires root for DMA/GPIO access)
sudo ./rpi_adc_stream

# Terminal 2: Extract and display data
./rpi_adc_bufextract

# OR: OpenGL visualization (recommended)
./test-ui

# OR: Web-based visualization (OBSOLETE - broken, needs migration to shared memory)
# python3 adc_server.py  # Do not use - not adapted to ring buffer yet
```

**Important**: The streaming process must run as root to access `/dev/mem` for DMA and GPIO control.

## Architecture

### DMA-Based Acquisition

The core innovation is using **3 DMA channels working in lockstep**:
- **DMA Channel A**: PWM → triggers SPI at precise intervals (clock source)
- **DMA Channel B**: SPI TX FIFO → sends ADC read commands
- **DMA Channel C**: SPI RX FIFO → receives ADC data into memory buffer

See `rpi_adc_stream.c` functions: `init_spi_dma()` and `do_streaming()` for DMA setup.

**Why DMA?**: Achieves 1 MSPS without CPU intervention; traditional polling limited to ~100 KSPS.

### Lock-Free Ring Buffer (mvaring)

**File**: `mvaring.c` / `mvaring.h`

Custom lock-free ring buffer using **seqlock** pattern for single-writer/single-reader:
- Writer atomically increments `windex` and sets `writing` flag
- Reader uses seqlock retry logic to detect concurrent writes
- `dropped` counter tracks reader lag (overwritten entries)

**Critical Detail**: `writing` is `_Atomic uint8_t` (version 2) - this is NOT a traditional seqlock but a simplified variant optimized for single-reader/single-writer with retry on contention.

**Size**: Power-of-2 sizing enforced (`NUM_DATA_CHUNKS` must match `BUFF_MASK + 1`). Default: 8192 chunks.

### Shared Memory IPC

**File**: `rpi_shmem.c` / `rpi_shmem.h`

POSIX shared memory (`shm_open`) with name `/RPI_ADC_BUFF`:
- Writer creates with `shmem_create()` 
- Readers attach with `shmem_open()` or `shmem_open_ro()`
- Structure: `struct mvaring` (includes version, indices, and data array)

**Lifecycle**: Writer preserves shared memory on exit (unless `KEEP_SHM_BUF` undefined), allowing readers to reconnect. Use `shmem_destroy()` to explicitly clean up.

### Hardware Abstraction

**File**: `rpi_dma_utils.c` / `rpi_dma_utils.h`

Raspberry Pi hardware interface layer (based on Jeremy P Bentham's iosoft.blog):
- Memory-mapped peripheral access via `/dev/mem`
- VideoCore mailbox for uncached DMA memory allocation
- GPIO, PWM, SPI, and DMA register control

**Platform**: `RPI_VERSION` hardcoded to 4 (see `#define RPI_VERSION 4`). Different register bases for Pi 0/1/2/3/4.

### Configuration Constants

**File**: `common.h`

- `MAX_SAMPS`: Samples per chunk (default: 1024)
- `NUM_DATA_CHUNKS`: Ring buffer depth (default: 0x2000 = 8192)
- `BUFF_MASK`: Must equal `NUM_DATA_CHUNKS - 1`
- `SPINAWHILE()`: Busy-wait macro - `yield` on ARM, `pause` on x86, or `sched_yield()` if not `BE_LAZY`

**Speed Modes** (`rpi_adc_stream.c`):
- `HI_SPEED`: 1 MSPS @ 20 MHz SPI
- `LO_SPEED`: 100 KSPS @ 2 MHz SPI

Comment/uncomment `#define HI_SPEED` or `#define LO_SPEED` to switch.

## Coding Conventions

### Code Style

**Preferred**: Linux kernel coding style for new C files
- Tabs (not spaces) for indentation
- K&R brace style (opening brace on same line for functions, control structures)
- 80-column line limit where practical
- Reference: https://www.kernel.org/doc/html/latest/process/coding-style.html

**Legacy files**: Existing code does not follow this convention consistently - do not refactor old files just for style.

### Naming

- **Global variables**: `g_` prefix (e.g., `g_sample_rate`, `g_pwm_freq`)
- **Pointer parameters**: `*mp`, `*rp`, `*cbp` suffixes common for "map pointer", "register pointer", "control block pointer"
- **Macros**: SCREAMING_SNAKE_CASE for constants, `MACRO()` for function-like macros

### Memory Management

- **Uncached memory**: All DMA buffers allocated via `map_uncached_mem()` (VideoCore mailbox API)
- **Cleanup on signals**: SIGINT/SIGTERM handlers call `terminate()` → `stop_dma()` → `unmap_*()` → `shmem_close()`
- **Shared memory persistence**: Writer does NOT unlink shared memory by default on exit (readers can continue accessing buffered data)

### Error Handling

Use `fail()` function (variadic, like printf) for fatal errors:
```c
if (!ring_init(...))
    fail("Ring buffer init failed");
```

Exits with code 1 and prints to stderr.

### Atomic Operations

C11 `stdatomic.h` used in `mvaring.h`:
- `_Atomic uint8_t writing`
- `atomic_uint rindex, windex`

**No manual memory barriers needed** - atomic operations provide necessary ordering.

## Important Implementation Details

### DMA Control Block Chaining

DMA uses **circular linked list** of control blocks (CBs):
- Each CB's `next_cb` points to next CB (bus address, not virtual)
- Last CB points back to first → infinite loop
- Must be 32-byte aligned (`__attribute__ ((aligned(32)))`)

See `adc_dma_init()` in `rpi_adc_stream.c` for CB setup.

### SPI Timing

**ADC Requirements**:
- **MCP3202** (legacy): Minimum clock period 400ns (2.5 MHz max), runs at 20 MHz
- **BD7910x** (target): Also uses 20 MHz SPI clock, 1 MSPS sample rate
- Actual SPI freq: 20 MHz / divider
- Divider must be power-of-2 for clean sampling

**Calculation**: `SPI_CLOCK_HZ / (2 * spi_freq)` rounds to nearest power-of-2.

### Sample Rate Control

Controlled by **PWM frequency**, not SPI frequency:
- PWM triggers SPI transactions via DMA
- PWM freq = desired sample rate
- `init_pwm(freq, range, value)` sets up clock

**BCM2711 SPI0 timing registers — none configurable**:
BCM2711 SPI0 (`rpi_adc_stream` uses SPI0 at base `0x7e204000`) has **no programmable
CS timing registers**:
- CS setup/hold in DMA mode: fixed **3 core clock cycles** before first bit, **1 core
  cycle** after last clock (§9.6.4 "Notes", p.141). At ~250 MHz core = 12 ns / 4 ns.
- Minimum CS high time between transfers: **1 SPI clock period** = 50 ns at 20 MHz
  (hardware-enforced floor, not a register).
- `LTOH` register (offset `0x10`): LoSSI mode only, not applicable.

The **AUX SPI1/2** (BCM2711 §2.3) do have more controls (CS high time, DOUT hold time)
but the project uses SPI0, so these are not available.

**BCM2711 SPI0 SPI_DLEN is a down-counter — MUST be rewritten every cycle**:
`SPI_DLEN` (offset `0x0C`) counts down to zero as bytes are transferred (§9.5 p.139:
"Peripheral generates data requests … until the SPIDLEN has been reached"). After a
transfer completes DLEN = 0 and stays there. Writing TA=1 (SPI_CS) with DLEN=0
triggers an immediate 0-byte "transfer": brief SCLK glitch, no CS assertion, no data.
**DMA CB8 (which writes DLEN before each SPI trigger) cannot be skipped.**

**GPIO-CE mode with SPI_AUTO_CS (ADCS=1) — both must be present together**:
The project holds CE0 (GPIO8) LOW for the entire acquisition via `GPIO_OUT` (GPIO-CE
mode) rather than letting the SPI controller toggle CE each transfer.  However, ADCS=1
(`SPI_AUTO_CS`) **must still be set** in `adc_csd` (written to `SPI_CS` by CB9).

Why ADCS=1 matters (timing mitigation, not a full fix):
- CB6 is the TX DMA chain; it loops forever writing `txd[0]=0xD0` to `SPI_FIFO`.
- BCM2711 §9.5 p.139 (SPI_FIFO, DMA mode): *"If TA is clear, the first 32-bit write
  to this register will control SPIDLEN and SPICS."*
- When CB6 fires during the `TA=0` inter-frame window, `0x000000D0` is interpreted as
  a **control word**: DLEN=0, CPOL=0 (wrong), CPHA=0 (wrong), TA=1 → starts spurious
  transfer with wrong SPI mode; SCLK transitions from HIGH to LOW.
- **BCM2711 §9.5 Table 165 (DC register TDREQ) does NOT list TA as a gating condition
  for TX DREQ.** ADCS=1 does NOT suppress TX DREQ during `TA=0` per the spec.
- ADCS=1 helps via timing: with DLEN=0 spurious frame + ADCS=1, the SPI delays SCLK
  by 3 core clock cycles (CS setup, §9.6.4 p.141). Since DLEN=0, the frame ends
  within those cycles before SCLK can transition → glitch is invisible.
- With ADCS=0, no such guard exists → repeated SCLK glitches → 13–22 pulses/burst.
- This mitigation may be unreliable at ≥1 MSPS.

The root cause is that CB6 does not follow the BCM2711-intended TX DMA pattern
(§9.6.3): write `(DLEN_bytes<<16)|SPICS[7:0]` as a control word first, then data,
then stop. A proper fix would restructure the DMA chain per §9.6.3.
See `docs/spi-dma-adcs-ta-dreq-analysis.md` for full analysis.

With CE0 in `GPIO_OUT` mode (ALT0 disconnected), ADCS=1 has **no effect on the
physical CE0 pin** — the SPI's CE0 output is disconnected. The GPIO holds CE0 LOW.

**Practical SPI0 throughput ceiling**:
At 20 MHz SPI with 16-bit samples: ~800 ns transfer + CB8+CB9 DMA overhead (~100–300 ns
bus latency with 3 concurrent DMA channels) + ~50 ns min CS high = **~950–1150 ns
minimum cycle time**. In practice ~400–600 kHz is the observed ceiling due to AXI bus
contention.

**PWM DREQ threshold semantics (confirmed: `≤` less-than-or-equal)**:
The BCM2711 PWM DMAC DREQ field uses `DREQ = (fifo_level ≤ threshold)`.
With threshold=1 (the project default), DREQ fires when the FIFO holds **0 or 1**
words.
- **Startup transient**: when PWM starts it immediately consumes **1 word** from
  the FIFO (BCM2711 §8.4 p.128; GAPO bit in §8.6).  This is a single-word
  pipeline — there is **no double-buffer** documented in the BCM2711 datasheet.
  With N_PRIME=2 and threshold=1: after 1 consumed word, FIFO=1 satisfies `1≤1`
  → DREQ fires immediately.  With N_PRIME=1: FIFO=0 → DREQ fires; DMA refills
  to 1; `1≤1` still fires → 2 consecutive immediate fires before paced timing.
- Steady-state FIFO oscillates **2→1→2→1** per PWM period.
- The serialiser **never stalls** (FIFO never reaches 0 in steady state).
- SPI TDREQ (§9.5 Table 165, p.139) also uses `≤` — consistent with PWM.
- Note: https://raspberrypi.stackexchange.com/questions/73379 (likely older chip)
  concluded `<`; BCM2711 is definitively `≤` per empirical test.
- Confirmed by `ai-gen-tests/test_pwm_dreq.c`; see `docs/bcm2711-pwm-configuration.md` §8.2.

### Data Format

**Raw ADC data** (MCP3202 legacy format): 12-bit values in 16-bit words (big-endian over SPI)

Extraction macro:
```c
#define ADC_RAW_VAL(d) (((uint16_t)(d)<<8 | (uint16_t)(d)>>8) & 0x7ff)
```

**Note**: Comment says 11-bit (`0x7ff`) but MCP3202 is actually 12-bit - this masks to 11 bits for some reason (possible hardware quirk or intentional).

**BD7910x format**: Will differ from MCP3202 - check datasheet for bit layout and endianness.

### Visualization

**OpenGL** (`test-ui`): 
- Direct framebuffer rendering, update loop pulls from shared memory ring buffer
- **Status**: Actively maintained, works with current shared memory implementation

**WebGL** (`webgl_graph.html` + `adc_server.py`): 
- **Status**: OBSOLETE - Not actively maintained
- **Issue**: Still uses old FIFO-based approach, not adapted to shared memory ring buffer
- Original design: Server polled FIFO, served data as CSV over HTTP, client rendered with WebGL line strips
- Do not rely on this for new development

## Common Tasks

### Changing Sample Rate

Edit `rpi_adc_stream.c`:
```c
#define SAMPLE_RATE MEGA(1)  // or KILO(100) for 100k samples/sec
```

Rebuild: `make clean && make`

### Changing Buffer Size

Edit `common.h`:
```c
#define MAX_SAMPS 1024        // samples per chunk
#define NUM_DATA_CHUNKS 0x2000 // number of chunks (must be power of 2)
#define BUFF_MASK 0x1FFF       // NUM_DATA_CHUNKS - 1
```

**Verify power-of-2**: Build fails if `NUM_DATA_CHUNKS & BUFF_MASK != 0`

### Adding Debug Output

- Use `#if DEBUG` blocks (defined in `rpi_dma_utils.h`)
- Or compile debug build: `make dbg` and use GDB

### Porting to Different Pi Model

Edit `rpi_dma_utils.h`:
```c
#define RPI_VERSION 3  // Change from 4 to target model
```

Updates peripheral base addresses and clock frequencies automatically.

## References

- Blog: https://iosoft.blog/streaming-analog-data-raspberry-pi
- BCM2711 Datasheet: `docs/RP-008248-DS-1-bcm2711-peripherals.pdf`
- RPi 4 Datasheet: `docs/RP-008341-DS-1-raspberry-pi-4-datasheet.pdf`
- BD7910x Datasheet: `docs-not-to-git/` (**DO NOT commit files from this directory**)

### Documentation: Pre-converted Text Files

The PDF datasheets in `docs/` have been converted to plain text for faster searching:

| Text file | Source PDF | Use for |
|-----------|-----------|---------|
| `docs/RP-008248-DS-1-bcm2711-peripherals.txt` | BCM2711 peripherals datasheet | DMA, PWM, SPI, GPIO, Clock Manager, System Timer register details |
| `docs/RP-008341-DS-1-raspberry-pi-4-datasheet.txt` | RPi 4 product datasheet | Board-level pinout, feature overview |

**Always search the `.txt` files first** using `grep` — this is faster and avoids repeated `pdftotext` conversion:

```bash
grep -n "GPSET0\|GPCLR0" docs/RP-008248-DS-1-bcm2711-peripherals.txt
grep -n -A10 "PWM0_0" docs/RP-008248-DS-1-bcm2711-peripherals.txt
```

**For page number references**, open the original `.pdf` to verify: the text files do not preserve page numbers, so cross-reference using unique section headings or register names found in the `.txt` and then locate them in the PDF.

## Important File/Directory Rules

- **`docs-not-to-git/`**: Contains datasheets and documents that must NOT be pushed to git (proprietary/NDA materials)
- Verify `.gitignore` excludes this directory before committing

## License

Apache 2.0 (see file headers for copyright notices)
