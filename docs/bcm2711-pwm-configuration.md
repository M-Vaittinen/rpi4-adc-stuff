# BCM2711 PWM Configuration Guide

> **Reference document**: BCM2711 ARM Peripherals
> (`docs/RP-008248-DS-1-bcm2711-peripherals.pdf`)

---

## 1. Overview

The BCM2711 contains **two independent PWM controllers**, named **PWM0** and
**PWM1**.  Each controller has **two output channels** (channel 1 and channel
2), giving four independently-configurable PWM outputs in total.

Key characteristics (§8.1, p.127):

| Property | Value |
|---|---|
| Channels per controller | 2 |
| Controllers | PWM0, PWM1 |
| Clock source | `clk_pwm`, nominally **100 MHz** (configurable via CPRMAN) |
| FIFO depth | 64 × 32-bit words (shared between both channels) |
| Output modes | PWM (even-spread or M/S ratio), Serialiser |

### GPIO pin assignments (§8.5, p.128)

| GPIO | Alt function | Signal |
|---|---|---|
| GPIO12 | ALT0 | PWM0_0 |
| GPIO13 | ALT0 | PWM0_1 |
| GPIO18 | ALT5 | PWM0_0 |
| GPIO19 | ALT5 | PWM0_1 |
| GPIO40 | ALT0 | PWM1_0 |
| GPIO41 | ALT0 | PWM1_1 |
| GPIO45 | ALT0 | PWM0_1 |

---

## 2. PWM Clock Configuration (CPRMAN)

The PWM clock is controlled by the **Clock Manager** (CPRMAN), not by the PWM
peripheral itself.  The registers live at:

```
CLK_BASE = PHYS_REG_BASE + 0x101000   (= 0xFE101000 on Raspberry Pi 4)
```

The two relevant registers (§5.4, p.82):

| Offset | Name | Description |
|---|---|---|
| `0xa0` | `CM_PWMCTL` | PWM Clock Control |
| `0xa4` | `CM_PWMDIV` | PWM Clock Divisor |

> **Note**: The General Purpose Clock register table in §5.4 documents offsets
> `0x70`–`0x84` for GP clocks.  The PWM clock lives at the adjacent offsets
> `0xa0`/`0xa4` and shares the identical bit-layout.

### CM_PWMCTL / CM_PWMDIV bit layout

`CM_PWMCTL` (same layout as `CM_GP0CTL`, Table 99, p.82):

| Bits | Name | Description |
|---|---|---|
| 31:24 | PASSWD | Must write `0x5A` to allow any write |
| 10:9 | MASH | Noise-shaping: 0=integer, 1=1-stage, 2=2-stage, 3=3-stage |
| 8 | FLIP | Invert output (test/debug only) |
| 7 | BUSY | Read-only; 1 = clock is running |
| 5 | KILL | Force stop (test/debug only) |
| 4 | ENAB | Enable/disable the clock generator (glitch-free) |
| 3:0 | SRC | Clock source select (see below) |

`CM_PWMDIV` (same layout as `CM_GP0DIV`, Table 100, p.82):

| Bits | Name | Description |
|---|---|---|
| 31:24 | PASSWD | Must write `0x5A` |
| 23:12 | DIVI | Integer divisor |
| 11:0 | DIVF | Fractional divisor (for MASH) |

**Clock sources** (SRC field):

| Value | Source |
|---|---|
| 1 | Crystal oscillator (19.2 MHz) |
| 5 | PLLC per |
| 6 | PLLD per ← used in this project |
| 7 | HDMI auxiliary |

On **Raspberry Pi 4**, `PLLD per ≈ 750 MHz / 2 = 375 MHz`
(see `CLOCK_HZ = 375000000` in `rpi_dma_utils.h`).

### Initialisation sequence (safe, glitch-free)

```c
// 1. Stop clock (KILL=0; ENAB=0; keep password)
*REG32(clk_regs, CM_PWMCTL) = CLK_PASSWD | (1 << 5);   // KILL=1 → stop
// 2. Wait until BUSY de-asserts
while (*REG32(clk_regs, CM_PWMCTL) & (1 << 7)) ;
// 3. Set divisor  (integer-only for clean edges)
*REG32(clk_regs, CM_PWMDIV) = CLK_PASSWD | (divi << 12);
// 4. Select PLLD (SRC=6), integer division (MASH=0), then ENAB=1
*REG32(clk_regs, CM_PWMCTL) = CLK_PASSWD | 6 | (1 << 4);
// 5. Wait for BUSY to assert (clock running)
while (!(*REG32(clk_regs, CM_PWMCTL) & (1 << 7))) ;
```

`CLK_PASSWD = 0x5A000000`.

**Never change SRC or MASH while BUSY=1** — doing so can cause glitches or
clock lock-up (§5.4, p.82).

---

## 3. PWM Register Map

Both controllers share the same register layout.

| Controller | Base address (bus) | Base address (Pi 4 physical) |
|---|---|---|
| PWM0 | `0x7e20c000` | `0xFE20C000` |
| PWM1 | `0x7e20c800` | `0xFE20C800` |

(§8.6 Table 152, p.129)

| Offset | Name | Description |
|---|---|---|
| `0x00` | CTL | Control register |
| `0x04` | STA | Status register |
| `0x08` | DMAC | DMA Configuration register |
| `0x10` | RNG1 | Channel 1 Range |
| `0x14` | DAT1 | Channel 1 Data |
| `0x18` | FIF1 | FIFO Input (write-only, shared) |
| `0x20` | RNG2 | Channel 2 Range |
| `0x24` | DAT2 | Channel 2 Data |

---

## 4. Modes of Operation

(§8.4, p.128)

### 4.1 PWM mode — even-spread (MSEN=0, MODE=0)

The default.  An internal context counter spreads N high-pulses evenly across
M clock cycles (the algorithm from §8.3, p.127):

```
context = context + N
if context >= M:  output = 1,  context -= M
else:             output = 0
```

This gives the best approximation of duty cycle `N/M` at all times.

- `RNGi` = M (period, in PWM clock cycles)
- `DATi` or FIFO word = N (number of high-pulses per period)
- Output frequency = `clk_pwm / RNGi`

### 4.2 PWM mode — M/S ratio (MSEN=1, MODE=0)

Simpler: output is high for the first M cycles, low for the rest.  Preferred
when high-frequency spectral spreading is unwanted.

### 4.3 Serialiser mode (MODE=1)

Each 32-bit word from `DATi` or the FIFO is clocked out MSB-first, one bit per
PWM clock cycle.  `RNGi` controls padding: if RNG < 32, the word is truncated;
if RNG > 32, zero-bits are appended.  Used (for example) to generate precise
SPI-like bit streams via DMA.

---

## 5. CTL Register — Bit Reference

(§8.6 Table 153, p.129–130)

### Channel 1 bits

| Bit | Name | Description |
|---|---|---|
| 0 | PWEN1 | Channel 1 enable (1 = enabled) |
| 1 | MODE1 | 0 = PWM mode, 1 = Serialiser mode |
| 2 | RPTL1 | Repeat last FIFO word when FIFO empties (prevents gap) |
| 3 | SBIT1 | Silence/idle output bit (also zero-padding in serialiser) |
| 4 | POLA1 | Invert output polarity |
| 5 | USEF1 | 0 = use DAT1, 1 = use FIFO |
| 6 | CLRF | Clear FIFO — write-1-to-clear, one-shot, always reads 0 |
| 7 | MSEN1 | 0 = even-spread PWM, 1 = M/S ratio |

### Channel 2 bits

| Bit | Name | Description |
|---|---|---|
| 8 | PWEN2 | Channel 2 enable |
| 9 | MODE2 | Mode select |
| 10 | RPTL2 | Repeat last data |
| 11 | SBIT2 | Silence bit |
| 12 | POLA2 | Polarity |
| 13 | USEF2 | Data source |
| 15 | MSEN2 | M/S or even-spread |

---

## 6. STA Register — Status Flags

(§8.6 Table 154, p.130–131)

| Bit | Name | Type | Description |
|---|---|---|---|
| 0 | FULL1 | RO | FIFO full |
| 1 | EMPT1 | RO | FIFO empty |
| 2 | WERR1 | W1C | Write-when-full error |
| 3 | RERR1 | W1C | Read-when-empty error |
| 4 | GAPO1 | W1C | Gap in channel 1 output (FIFO ran dry) |
| 5 | GAPO2 | W1C | Gap in channel 2 output |
| 8 | BERR | W1C | Bus error (APB write synchronisation fault) |
| 9 | STA1 | RO | Channel 1 is currently transmitting |
| 10 | STA2 | RO | Channel 2 is currently transmitting |

Clear sticky flags (WERR, RERR, GAPO, BERR) by writing 1 to the relevant bit.

---

## 7. RNG, DAT, and FIF Registers

(§8.6 Tables 156–158, p.131–132)

**RNG1 / RNG2** (offsets `0x10`, `0x20`):

- 32-bit range value M.  Default = 32.
- In PWM mode: defines the period (output frequency = `clk_pwm / RNG`).
- In serialiser mode: defines the transmission window; truncates or zero-pads
  the 32-bit data word.

**DAT1 / DAT2** (offsets `0x14`, `0x24`):

- Used when `USEFi = 0`.
- PWM mode: N pulses per period.
- Serialiser mode: bit pattern to serialise.

**FIF1** (offset `0x18`) — **write-only**:

- Shared FIFO input for both channels.
- When both channels use the FIFO (`USEF1=USEF2=1`), words alternate: odd
  words to channel 1, even words to channel 2.
- Always clear the FIFO (`CLRF=1`) before restarting to flush stale data.
- Reading always returns 0.

---

## 8. DMA DREQ Generation

### 8.1 How the DREQ mechanism works

(§4.2.1.3, p.61; §8.6 Table 155, p.131)

The PWM peripheral generates a **DREQ (Data Request)** signal to pace a DMA
engine.  The signal is **level-sensitive**: it stays high ("I need data") as
long as the FIFO occupancy is *below* the configured threshold, and goes low
once the threshold is met.

The DMA channel observes DREQ on the bus (via its `PERMAP` field) and:
- **Stalls** when DREQ is low (FIFO has enough data).
- **Resumes** when DREQ goes high (FIFO is below the threshold → needs refill).

Because writes to an APB peripheral are pipelined, the peripheral must have
enough spare FIFO capacity to absorb in-flight writes after DREQ goes low
(§4.2.1.3, p.61).

### 8.2 DMAC Register

(§8.6 Table 155, p.131)

| Bits | Name | Default | Description |
|---|---|---|---|
| 31 | ENAB | 0 | Must be 1 to enable PWM→DMA signalling |
| 15:8 | PANIC | 7 | FIFO threshold to assert the **Panic** signal |
| 7:0 | DREQ | 7 | FIFO threshold to assert the **DREQ** signal |

The DREQ signal goes **active** (high) when:

```
FIFO occupancy ≤ DREQ threshold   ("less-than-or-equal", empirically confirmed on BCM2711)
```

> **Note on threshold semantics and startup transient**:
> The BCM2711 PWM DMAC register description (§8.6 Table 155, p.131) only states
> that DREQ is the "threshold level for DREQ signal going active" without giving
> the comparison operator.  Empirical testing on RPi 4
> (`ai-gen-tests/test_pwm_dreq.c`) confirms:
>
> - The comparison is **`≤`** (less-than-or-equal): DREQ fires when
>   `fifo_level ≤ threshold`.  With threshold=1, DREQ fires when the FIFO
>   contains 0 **or 1** words.
>
> - The SPI TDREQ field (§9.5 Table 165, p.139) also uses `≤` — semantics
>   are **consistent** across these two BCM2711 peripherals.
>
> - **Startup transient**: when PWM is enabled, the hardware immediately consumes
>   one word from the FIFO.  This is described in BCM2711 §8.4 p.128:
>   *"channel sends continuously as long as FIFO is not empty"*, and the STA
>   register GAPO bit (§8.6): *"after the state machine has sent a word and is
>   waiting for the next word."*  There is **no double-buffer** documented in
>   the BCM2711 datasheet.  The startup transient is fully explained by `≤`
>   semantics: with N_PRIME=2 pre-loaded words and threshold=1, after the one
>   startup-consumed word the FIFO is at 1, which satisfies `1 ≤ 1` → DREQ
>   fires immediately (confirmed by the test).
>
> - Note: https://raspberrypi.stackexchange.com/questions/73379 (likely tested
>   on an older BCM chip) concluded `<` semantics.  BCM2711 empirically uses
>   `≤`.  Always verify for each chip/peripheral.
>
> For threshold=1 (used in `rpi_adc_stream`): the FIFO oscillates **2→1→2→1**
> in steady state; the serialiser never stalls.  The first-DREQ delay formula
> is: `delay = max(0, N_PRIME − threshold − 1) periods`.

The PANIC signal goes active when:

```
FIFO occupancy < PANIC threshold
```

PANIC raises the AXI priority of the associated DMA channel to allow it to
pre-empt lower-priority transfers and refill the FIFO urgently.

**Typical configuration for single-word FIFO pacing** (used in this project):

```c
// ENAB=1, PANIC=0, DREQ=1: DREQ fires when FIFO ≤ 1 (0 or 1 words)
*REG32(pwm_regs, PWM_DMAC) = PWM_DMAC_ENAB | 1;   // = 0x80000001
```

With threshold=1 and `≤` semantics, DREQ asserts when the FIFO holds 0 or 1
words.  The DMA refills with one word (raising FIFO 1→2); DREQ de-asserts.
The FIFO oscillates **2→1→2→1** each PWM period.  The serialiser **never
stalls** since the FIFO never reaches 0 in steady state.

### 8.3 PERMAP value for DMA Control Block

(§4.2.1.3, p.61; DREQ table)

| DREQ ID | Peripheral |
|---|---|
| **5** | **PWM0** |
| **1** | PWM1 (muxed with DSI0) |

To pace DMA writes *to* the PWM FIFO, set the DMA Control Block Transfer
Information (`TI`) word with:

- `DEST_DREQ` (bit 6) = 1 — gating on destination DREQ
- `PERMAP` (bits 20:16) = **5** for PWM0

```c
#define PWM_TI  (DMA_DEST_DREQ | (5 << 16) | DMA_WAIT_RESP)
```

`DMA_WAIT_RESP` ensures the DMA stalls until the write to `PWM_FIF1` is
acknowledged, preventing a second write from overtaking the first.

### 8.4 End-to-end pacing example (1 MSPS ADC sampling)

The project uses PWM as a **precision timing source** for ADC sampling, not as
an audio output:

1. `clk_pwm` is set to `CLOCK_HZ = 375 MHz` (PLLD, SRC=6, divisor=1 via
   CPRMAN registers at `CLK_BASE + 0xa0/0xa4`).
2. `PWM_RNG1 = 2` → PWM period = 2 PWM-clock cycles.
3. `PWM_FIF1 = 1` (single word) and `USEF1=1` (serialiser from FIFO).
4. `PWM_DMAC = 0x80000001` (ENAB=1, DREQ threshold = 1).

When the serialiser drops the FIFO to 1 word (level 2→1, satisfying `1 ≤ threshold=1`),
DREQ fires.  DMA channel A writes a fresh word to `PWM_FIF1`
(level 1→2), DREQ de-asserts, and the cycle repeats at exactly **1 MSPS**.
The FIFO oscillates **2→1→2→1**; the serialiser never stalls.

Channels B and C ride in the same DREQ window to trigger the SPI ADC read,
achieving sample-accurate timing without any CPU involvement.

---

## 9. Minimal Configuration Recipe

### Step 1 — Set PWM clock (CPRMAN)

```c
int divi = CLOCK_HZ / desired_pwm_freq;   // e.g. 375e6 / 1e6 = 375
*REG32(clk_regs, CM_PWMCTL) = CLK_PASSWD | (1 << 5); // stop
while (*REG32(clk_regs, CM_PWMCTL) & (1 << 7)) ;     // wait BUSY=0
*REG32(clk_regs, CM_PWMDIV) = CLK_PASSWD | (divi << 12);
*REG32(clk_regs, CM_PWMCTL) = CLK_PASSWD | 6 | (1 << 4); // SRC=PLLD, ENAB
while (!(*REG32(clk_regs, CM_PWMCTL) & (1 << 7))) ;  // wait BUSY=1
```

### Step 2 — Configure the PWM channel

```c
*REG32(pwm_regs, PWM_CTL)  = (1 << 6);              // CLRF: clear FIFO
*REG32(pwm_regs, PWM_RNG1) = range;                  // period in clk_pwm cycles
*REG32(pwm_regs, PWM_FIF1) = initial_value;          // prime the FIFO
```

### Step 3 — Enable DMA signalling (if using DMA)

```c
*REG32(pwm_regs, PWM_DMAC) = (1 << 31) | dreq_threshold; // ENAB + threshold
```

### Step 4 — Enable the channel

```c
// USEF1=1 (use FIFO), PWEN1=1 (enable)
*REG32(pwm_regs, PWM_CTL) = (1 << 5) | (1 << 0);   // 0x21
```

---

## 10. Document References

| Topic | Section | Page |
|---|---|---|
| PWM overview and algorithm | §8.1 – §8.3 | 127 |
| PWM modes of operation | §8.4 | 128 |
| GPIO pin assignments, DMA channel mapping | §8.5 Quick Reference | 128 |
| CTL register | §8.6 Table 153 | 129–130 |
| STA register | §8.6 Table 154 | 130–131 |
| DMAC register | §8.6 Table 155 | 131 |
| DREQ threshold: `<` semantics + 1-word startup consume (empirical + StackExchange) | `ai-gen-tests/test_pwm_dreq.c`, https://raspberrypi.stackexchange.com/questions/73379 | — |
| SPI TDREQ field (uses `≤`, different from PWM) | §9.5 Table 165 TDREQ | 139 |
| RNG1/RNG2 registers | §8.6 Table 156 | 131 |
| DAT1/DAT2 registers | §8.6 Table 157 | 132 |
| FIF1 register | §8.6 Table 158 | 132 |
| Peripheral DREQ signal table | §4.2.1.3 | 61 |
| DMA DREQ pacing mechanism | §4.2.1.3 | 61 |
| Clock Manager (CPRMAN) GP clock registers | §5.4 Table 99–100 | 82–83 |
