# BCM2711 SPI DMA: ADCS, TA, TX DREQ, and the SPI_FIFO Control-Word Trap

**Audience**: Developers maintaining `rpi_adc_stream.c`.  
**Motivation**: Understanding why `SPI_AUTO_CS` (ADCS=1) is kept in `adc_csd` even
when CE0 is driven by GPIO, and whether it fully prevents SPI state corruption.

---

## 1. The BCM2711 SPI_FIFO Control-Word Mechanism

BCM2711 ARM Peripherals §9.5 (SPI Register Map), **SPI_FIFO register**, offset 0x04:

> **DMA Mode (DMAEN set):**  
> If TA is clear, the first 32-bit write to this register will control SPIDLEN and
> SPICS. Subsequent reads and writes will be taken as four-byte data words to be
> read/written to the FIFOs.

(BCM2711 p.139, Table 163 DATA field.)

Decoding the 32-bit control word:

| Bits  | Target register field | Notes |
|-------|-----------------------|-------|
| 31:16 | `SPIDLEN` (offset 0x0C) | Transfer length in bytes |
| 7:0   | `SPICS[7:0]` (offset 0x00) | Lower byte of SPI_CS: TA, CSPOL, CLR, CPOL, CPHA, CS |

**Purpose (intended use):** This mechanism lets a TX DMA chain set both `DLEN` and
`SPICS` (including `TA=1` to start the transfer) with a **single write** to
`SPI_FIFO`, without requiring a separate DMA channel to write to `SPI_CS` and
`SPI_DLEN` registers directly. The BCM2711 §9.6.3 DMA mode procedure relies on this:

> 3. DMA channel 1 Control Block should have its PERMAP set to SPIn TX and should be
>    set to write **'transfer length' + 1 words** to SPI_FIFO. The data should
>    comprise:  
>    a. A word with the **transfer length in bytes in the top sixteen bits**, and the
>       **control register settings [7:0] in the bottom eight bits** (i.e. TA = 1, CS,
>       CPOL, CPHA as required.).  
>    b. 'Transfer length' number in words of data to send.

(BCM2711 p.140, §9.6.3 step 3.)

In this intended pattern, the TX DMA writes **exactly** N+1 words per frame and then
**stops**. The control word (word 0) is written when TA=0 and starts each new frame
by also setting TA=1. The subsequent N data words go into the TX FIFO normally.

---

## 2. How `rpi_adc_stream` Deviates from the Intended Pattern

`rpi_adc_stream` uses a **different** (non-standard) DMA architecture:

- **CB8** (PWM-paced): writes `DLEN=2` directly to the `SPI_DLEN` register.
- **CB9** (PWM-paced): writes `adc_csd` (with `TA=1`) directly to the `SPI_CS`
  register to start each transfer.
- **CB6** (TX DMA chain): loops *continuously*, writing `txd[0]` and `txd[1]` to
  `SPI_FIFO` using `SPI_TX_DREQ` flow control. It **never stops**.

The code does not follow the BCM2711-intended "N+1 words, then stop" pattern. CB8
and CB9 replace the per-frame setup that would otherwise be done by the control word.
CB6 is a perpetual data pump.

---

## 3. TX DREQ Generation: What the Spec Says

BCM2711 §9.5 (SPI Register Map), **DC Register** (offset 0x14), `TDREQ` field
(bits 7:0), p.139:

> **DMA Write Request Threshold.**  
> Generate a DREQ signal to the TX DMA engine whenever the TX FIFO level is **less
> than or equal to** this amount.

And from the `DMAEN` field of `SPI_CS`, bit 8 (p.138):

> **DMA Enable.**  
> 0 = No DMA requests will be issued.  
> 1 = Enable DMA operation. Peripheral generates data requests. These will be taken
> in four-byte words until the SPIDLEN has been reached.

**Key observations from the spec:**

1. TX DREQ depends on **DMAEN** and **TX FIFO level ≤ TDREQ**.
2. The TDREQ description has **no explicit mention of TA** as a gating condition.
   The spec does not say "TX DREQ is suppressed when TA=0."
3. The DMAEN description says requests are generated "until the SPIDLEN has been
   reached," but does **not** state that DMAEN is automatically cleared when SPIDLEN
   is reached.
4. DMAEN is a read/write register field (reset 0x0) and no hardware auto-clear is
   described for it at frame end.

**Conclusion from the spec alone:** TX DREQ may be asserted whenever `DMAEN=1` and
`TX_FIFO_LEVEL ≤ TDREQ`, potentially *including* the `TA=0` inter-frame window.
The spec is **ambiguous** about whether TA gates TX DREQ.

---

## 4. What Happens When CB6 Fires During TA=0

After each 2-byte SPI transfer completes:

1. `dma_frame_end` fires (BCM2711 §9.5 TA field, p.138: *"TA is cleared by a
   dma_frame_end pulse from the DMA controller."*) → **TA goes to 0**.
2. TX FIFO becomes empty (0 bytes). Since `TDREQ` default reset value is `0x20` (32;
   BCM2711 Table 165), and 0 ≤ 32, **TX DREQ is asserted**.
3. CB6 is serviced: writes `txd[0] = 0x000000D0` to `SPI_FIFO`.
4. Because **DMAEN=1 AND TA=0**: the write is treated as a **control word**:

| Field | Value | Meaning |
|-------|-------|---------|
| `SPIDLEN` (bits 31:16) | `0x0000` = 0 bytes | Spurious transfer length = 0 |
| `SPICS[7]` = TA | `1` | **Starts a spurious transfer** |
| `SPICS[6]` = CSPOL | `1` | CS active-HIGH (wrong) |
| `SPICS[4]` = CLR_TX | `1` | Clears TX FIFO |
| `SPICS[3]` = CPOL | `0` | **SCLK idles LOW** (was HIGH for SPI mode 3) |
| `SPICS[2]` = CPHA | `0` | Sample on first edge (was second) |

**Effect:** SCLK transitions from HIGH (correct idle) to LOW (wrong idle). CB9 later
writes the correct `adc_csd` with CPOL=1, causing SCLK to go HIGH again. The logic
analyser sees the LOW→glitch→HIGH sequence as phantom clock pulses.

---

## 5. What ADCS Does and Does Not Do

BCM2711 §9.5, `ADCS` field of `SPI_CS`, bit 11 (p.137):

> **Automatically De-assert Chip Select.**  
> 0 = Don't automatically de-assert chip select at the end of a DMA transfer; chip
>     select is manually controlled by software.  
> 1 = Automatically de-assert chip select at the end of a DMA transfer (as
>     determined by SPIDLEN).

BCM2711 §9.6.4 Notes (p.141):

> Setup and Hold times related to the automatic assertion and de-assertion of the CS
> lines when operating in DMA mode (DMAEN and ADCS set) are as follows:
> - The CS line will be **asserted at least 3 core clock cycles** before the MSB of
>   the first byte of the transfer.
> - The CS line will be **de-asserted no earlier than 1 core clock cycle** after the
>   trailing edge of the final clock pulse.

**What ADCS=1 does:**
- Automatically deasserts the CE pin after each transfer (if CE is in SPI ALT0 mode).
- Enforces 3+1 core-cycle CS setup/hold delays (§9.6.4).

**What ADCS=1 does NOT do (per the spec):**
- It is **not documented as suppressing TX DREQ** during the `TA=0` inter-frame
  window.
- It does not clear DMAEN at frame end.
- In GPIO-CE mode (`GPIO8` is `GPIO_OUT`, not `ALT0`): ADCS=1 has **zero effect on
  the physical CE0 pin**.

---

## 6. Why ADCS=1 Mitigates the Problem in Practice (Hypothesis)

Even though ADCS=1 doesn't suppress TX DREQ when TA=0, it changes the *outcome* of
the spurious control-word write by adding the 3+1 core-cycle CS timing constraint:

**With ADCS=0, DLEN=0 spurious write:**
1. Control word sets DLEN=0, TA=1, CPOL=0, CPHA=0 → SCLK goes LOW.
2. DLEN=0 → frame ends immediately → `dma_frame_end` → TA=0.
3. TX DREQ fires again → CB6 fires again → another control word → another spurious
   frame. This oscillation can repeat many times before CB9 (PWM-paced) writes the
   correct `adc_csd`.
4. The repeated SCLK transitions produce many phantom pulses visible on a logic
   analyser as erratic bursts of 13–22 pulses instead of a steady 16.

**With ADCS=1, DLEN=0 spurious write:**
1. Control word sets DLEN=0, TA=1, CPOL=0, CPHA=0 → SCLK starts to go LOW.
2. The BCM2711 **holds off SCLK for 3 core clock cycles** (CS setup time, §9.6.4).
   At a core clock of ~250 MHz, this is ~12 ns.
3. Within those 12 ns, DLEN=0 is immediately satisfied → `dma_frame_end` → TA=0.
4. The SCLK never actually transitions (the frame completes *before* the first clock
   edge is generated). The CPOL glitch is sub-12 ns, typically below logic analyser
   resolution.
5. Result: the spurious frame is **invisible** on the logic analyser. Steady 16-pulse
   bursts are observed.

**Note:** BCM2711 §9.6.4 Note 2 confirms: *"SCLK is only generated during byte
serial transfer. It pauses in the rest state if the next byte to send is not ready or
RXF is set."* With DLEN=0 + ADCS=1, no byte is ever ready before the frame ends.

---

## 7. Why Occasional Hiccups at 1 MSPS Are Expected

At 500 kSPS (2 µs period), the typical inter-frame TA=0 window is ~100–300 ns (CB8 +
CB9 DMA chain overhead). The CB6 spurious write resolves within the ~12 ns ADCS=1
setup window. Timing margin is large.

At 1 MSPS (1 µs period), the timing is tighter. There are two failure modes:

1. **CB6 fires faster than DLEN=0 resolves with ADCS timing:** If the AXI bus is
   congested, the 3-cycle ADCS setup window may overlap with the next CB9 write.
   The SPI state can become inconsistent.

2. **The oscillation takes multiple cycles before CB9 wins:** Each spurious frame
   takes approximately 4+ core clocks (~16 ns) with ADCS=1. Multiple oscillations
   (CB6→control-word→dma_frame_end→CB6→...) may occur within the shorter TA=0
   window, eventually producing a visible SCLK glitch before CB9 restores order.

The user's observation of occasional hiccups at 1 MSPS with ADCS=1 is fully
consistent with this analysis.

---

## 8. The Root Cause: DMA Chain Design Mismatch

The fundamental problem is that CB6's design does not match the BCM2711's intended
SPI DMA pattern described in §9.6.3:

| Attribute | BCM2711 intended | `rpi_adc_stream` CB6 |
|-----------|-----------------|----------------------|
| Words per frame | N+1 (1 control + N data), then **stop** | Infinite loop |
| First word format | `(DLEN_bytes<<16) \| SPICS[7:0]` | `0x000000D0` (data byte) |
| TA=0 write | **Intended**: sets DLEN + starts frame | **Unintended**: corrupts CPOL/CPHA |
| Per-frame start | Via SPI_FIFO control word | Via CB8 (SPI_DLEN) + CB9 (SPI_CS) |

With the BCM2711-intended "write control-word-then-stop" pattern, CB6 would never
fire during TA=0 because the TX DMA exhausts its CB after each frame and stops.
The TX DREQ would fire after the frame ends, but no pending TX DMA CB exists to
service it. The next frame is started by software (or PWM-paced DMA) setting up a
new TX DMA CB.

---

## 9. Options for a Proper Fix

### Option A — Follow the BCM2711 Intended Pattern (Correct, Complex)

Restructure the TX DMA chain so that CB6 writes **exactly two words** per SPI frame
and stops (rather than looping):

- **Word 0** (written when TA=0): `(2<<16) | SPICS_LOWER_BYTE` where
  `SPICS_LOWER_BYTE` is `adc_csd[7:0]` with `TA=1`, correct `CPOL`, `CPHA`.
  This sets `DLEN=2` and starts the transfer in one write — making CB8 and CB9
  **redundant and removable**.
- **Word 1** (written when TA=1): the actual ADC TX data (`txd[0]` covering 2 bytes).

The PWM-paced DMA chain (CB7) would restart the TX DMA CB each period instead of
triggering CB8→CB9. TX DREQ firing during TA=0 would write the correct control word,
working as intended by the BCM2711.

### Option B — Break the CB6 Loop (Simpler)

Change CB6's `next_cb` to a "null" terminator after writing the TX data. The PWM
chain (CB7→CB8→CB9→CB7) would need to reset CB6's `next_cb` to itself before the
SPI transfer, then clear it after. This prevents CB6 from firing in the TA=0 window
because the CB is exhausted.

This requires careful synchronisation to avoid a race where CB6 exhausts before the
SPI transfer is complete.

### Option C — Current Mitigation (Imperfect)

Keep `SPI_AUTO_CS` (ADCS=1) in `adc_csd`. This reduces (but does not eliminate)
the visibility of spurious SCLK transitions by limiting each spurious frame to within
the 3-cycle CS setup window. CE0 in `GPIO_OUT` mode means ADCS=1 has no effect on
the physical pin.

This is the current implementation. It works reliably at ≤500 kSPS but may produce
occasional hiccups at 1 MSPS.

---

## 10. Summary Table

| Question | Answer | Spec reference |
|----------|--------|----------------|
| Does TX DREQ fire when TA=0? | **Spec is ambiguous.** TX DREQ is based on FIFO level ≤ TDREQ. TA is not listed as a gating condition. | BCM2711 §9.5 Table 165 TDREQ, p.139 |
| Does DMAEN auto-clear at frame end? | **Not documented.** Spec only says TA is cleared by `dma_frame_end`. | BCM2711 §9.5 TA field, p.138 |
| Does ADCS=1 suppress TX DREQ during TA=0? | **Not documented.** ADCS only controls CE pin deassert and CS setup/hold timing. | BCM2711 §9.5 ADCS field, p.137; §9.6.4, p.141 |
| Why does ADCS=1 help at 500 kSPS? | With DLEN=0 spurious frame + ADCS=1, the 3-cycle CS setup window holds off SCLK until the frame immediately ends. SCLK never actually transitions. | BCM2711 §9.6.4 Notes, p.141; §9.6.4 Note 2 |
| Is ADCS=1 a complete fix? | **No.** At 1 MSPS, sporadic hiccups are expected because the timing margin is smaller. | This document §7 |
| What is the proper fix? | Use the BCM2711-intended TX DMA pattern: write `(DLEN<<16)\|SPICS[7:0]` as the first word per frame, then stop. | BCM2711 §9.6.3 step 3, p.140 |

---

*BCM2711 ARM Peripherals document: RP-008248-DS-1.*
