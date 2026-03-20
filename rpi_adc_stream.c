// Raspberry Pi MCP3202 ADC streaming interface; see https://iosoft.blog for details
//
// Copyright (c) 2020 Jeremy P Bentham
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//	 http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// v0.20 JPB 16/11/20 Tidied up for first Github release

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "adc_common.h"
#include "common.h"
#include "rpi_helpers.h"
#include "mvaring.h"
#include "rpi_dma_utils.h"
#include "rpi_shmem.h"

#define VERSION "0.20"

#define MEGA(_meg) (_meg * 1000000LLU)
#define KILO(_kil) (_kil * 1000LLU)

/* Choose either hi or lo speed. Hi for 1MSPS, Lo for 100KSPS */
//#define LO_SPEED
#define HI_SPEED

#ifdef LO_SPEED
	#define SAMPLE_RATE	 KILO(100)     // Default & max sample rate (samples/sec)
	#define SPI_FREQ	MEGA(2)
#else
	#ifndef HI_SPEED
		#error "Either HI_SPEED or LO_SPEED is required"
	#endif
	#define SAMPLE_RATE	 MEGA(1)
//	#define SAMPLE_RATE	 KILO(100)
	#define SPI_FREQ	MEGA(20)
#endif

#define MAX_SAMPLE_RATE MEGA(1)

// SPI clock frequency
#define MIN_SPI_FREQ	10000
#define MAX_SPI_FREQ	MEGA(20)

// PWM definitions: divisor, and reload value
// PWM_VALUE is the seed word written to the FIFO before DMA takes over.
// It MUST equal pwm_val (the word written by DMA CB7) so the first PWM
// period produces the same duty cycle as steady state.  A mismatch causes
// an initial period at 100% (or 0%) duty cycle, which is visible as an
// anomalous pulse on the debug GPIO.
#define PWM_FREQ	    1000000
#define PWM_VALUE	    1	/* must equal .pwm_val in ADC_DMA_DATA */

// ADC sample size (2 bytes, with 11 data bits)
#define ADC_RAW_LEN	 2

// ADC and DAC chip-enables
#define ADC_CE_NUM	  0
#define DAC_CE_NUM	  1

// Definitions for 2 bytes per ADC sample (11-bit)
#define ADC_REQUEST(c)  {0xc0 | (c)<<5, 0x00}
#define ADC_VOLTAGE(n)  (((n) * 3.3) / 2048.0)
#define ADC_MILLIVOLTS(n) ((int)((((n) * 3300) + 1024) / 2048))
#define ADC_RAW_VAL(d)  (((uint16_t)(d)<<8 | (uint16_t)(d)>>8) & 0x7ff)

// Non-cached memory size
#define SAMP_SIZE	4
#define BUFF_LEN	(MAX_SAMPS * SAMP_SIZE)
#define MAX_BUFFS	2
// VC_MEM_SIZE: PAGE_SIZE for alignment + 2 ADC buffers + 2 GPIO buffers
#define VC_MEM_SIZE	(PAGE_SIZE + (BUFF_LEN * MAX_BUFFS * 2))

// DMA control block macros
#define NUM_CBS		12
#define REG(r, a)	REG_BUS_ADDR(r, a)
#define MEM(m, a)	MEM_BUS_ADDR(m, a)
#define CBS(n)		MEM_BUS_ADDR(mp, &dp->cbs[(n)])

/* GPIO trigger configuration
 * Trigger pin for external signal detection/timestamping
 * Reference: RP-008341-DS-1-raspberry-pi-4-datasheet.pdf
 * GPIO to 40-pin header mapping shown in datasheet section 1.2
 */
#define TRIGGER_PIN	 25	  // GPIO 25, Header Pin 22 (Bottom right area)

/*
 * Debug PWM output pin (GPIO12, header pin 32).
 *
 * GPIO12 ALT0 = PWM0_0 (BCM2711 peripherals datasheet §5, GPIO alternate
 * function table, p.92).  PWM0 channel 0 is the same PWM instance used for
 * DMA pacing (PERMAP=5, DMA_PWM_DREQ), so enabling this mux routes the
 * live pacing signal directly to the pin with no extra DMA load or AXI bus
 * traffic — the PWM peripheral drives the GPIO output entirely in hardware.
 *
 * Enabled at runtime with --pwm-debug (-d); off by default.
 *
 * SIGNAL CHARACTERISTICS
 * ======================
 * With the empirical ×2 factor in pwm_range (pwm_range=2) and the observed
 * PLLD_per ≈ 2×CLOCK_HZ (≈750 MHz on recent RPi 4 firmware), the actual
 * PWM clock is ≈2 MHz.  RNG1 = pwm_range = 2, DATA1 = pwm_val = 1, so:
 *
 *   duty cycle = DATA1 / RNG1 = 1/2 = 50%
 *   period     = RNG1 / actual_pwm_clock = 2 / 2 MHz = 1 µs  (1 MHz)
 *
 * Result: clean 1 MHz square wave with ~50% duty cycle.  No oversampling
 * needed, and the PWM clock/range used for DREQ pacing is unchanged.
 */
#define PWM_DBG_PIN		12	/* GPIO12, Header Pin 32, ALT0 = PWM0_0 */

/* GPIO trigger polarity - which level is considered "active"
 * 0 = active low (trigger when pin goes low)
 * 1 = active high (trigger when pin goes high)
 */
#define TRIGGER_POLARITY 1	  // Active high

// DMA transfer information for PWM and SPI
#define PWM_TI		(DMA_DEST_DREQ	| (DMA_PWM_DREQ << 16) | DMA_WAIT_RESP)
#define SPI_RX_TI	(DMA_SRCE_DREQ	| (DMA_SPI_RX_DREQ << 16) | DMA_WAIT_RESP | DMA_CB_DEST_INC)
#define SPI_TX_TI	(DMA_DEST_DREQ	| (DMA_SPI_TX_DREQ << 16) | DMA_WAIT_RESP | DMA_CB_SRCE_INC)
#define NOWAIT_RX_TI	(DMA_SRCE_DREQ 	| /* PERMAP */ 0	  | DMA_WAIT_RESP | 0)

// SPI 0 pin definitions
#define SPI0_CE0_PIN	8
#define SPI0_CE1_PIN	7
#define SPI0_MISO_PIN	9
#define SPI0_MOSI_PIN	10
#define SPI0_SCLK_PIN	11

// SPI registers and constants
#define SPI0_BASE	(PHYS_REG_BASE + 0x204000)
#define SPI_CS		0x00
#define SPI_FIFO	0x04
#define SPI_CLK		0x08
#define SPI_DLEN	0x0c
#define SPI_DC		0x14
#define SPI_FIFO_CLR	(3 << 4)
#define SPI_RX_FIFO_CLR	(2 << 4)
#define SPI_TX_FIFO_CLR	(1 << 4)
#define SPI_TFR_ACT	(1 << 7)
#define SPI_DMA_EN	(1 << 8)
#define SPI_AUTO_CS	(1 << 11)
#define SPI_RXD		(1 << 17)
#define SPI_CPOL	(1 << 3)
#define SPI_CPHA	(1 << 2)
#define SPI_CE0		0
#define SPI_CE1		1

// SPI DMA priority configuration (SPI_DC register)
// Format: RPANIC(31:24) | RDREQ(23:16) | TPANIC(15:8) | TDREQ(7:0)
// RX panic=8, RX req=1, TX panic=8, TX req=1
#define SPI_DMA_PRIORITY ((8<<24)|(1<<16)|(8<<8)|1)

// SPI register strings
//static char *g_spi_regstrs[] = {"CS", "FIFO", "CLK", "DLEN", "LTOH", "DC", ""};

/*
 * BCM2711 System Timer - free-running 1 MHz counter
 *
 * Register: ST_CLO (System Timer Counter Lower 32 bits)
 *   Bus address : 0x7E003004  (base 0x7E003000, offset 0x04)
 *   Phys addr   : 0xFE003004  on Raspberry Pi 4
 *   Access      : read-only, 32-bit
 *
 * The counter increments by 1 every microsecond.  It wraps around
 * from 0xFFFFFFFF to 0x00000000 after ~4295 s (~71 min).
 *
 * Clock source and firmware independence:
 *   The System Timer clock (BCM2835_CLOCK_TIMER) is derived from the
 *   19.2 MHz crystal oscillator ("xosc") via the BCM Clock Manager
 *   (CM_TIMERCTL / CM_TIMERDIV registers), not from any PLL.
 *   Consequently:
 *     - The 1 MHz rate is fixed and does NOT change with ARM CPU
 *       frequency scaling (DVFS) or VideoCore/GPU clock adjustments
 *       made by the Raspberry Pi firmware (start4.elf).
 *     - The only source of long-term frequency error is the crystal's
 *       own tolerance (typically ±20–50 ppm for standard crystals,
 *       ±1–5 ppm for precision types).
 *     - Short-term jitter comes from the integer clock divider
 *       (19.2 MHz / 19 = ~1.011 MHz with fractional correction), which
 *       is negligible for microsecond-resolution measurements.
 *
 * Reference: BCM2711 ARM Peripherals datasheet
 *   Chapter 10 "System Timer", pages 142–143
 *   File: docs/RP-008248-DS-1-bcm2711-peripherals.pdf
 *
 * Linux kernel confirmation:
 *   arch/arm/boot/dts/broadcom/bcm283x.dtsi — timer@7e003000 node,
 *   clock-frequency = <1000000>.
 *   drivers/clk/bcm/clk-bcm2835.c — BCM2835_CLOCK_TIMER registered
 *   with REGISTER_OSC_CLK (xosc parent), confirming crystal derivation.
 */
#define USEC_BASE	(PHYS_REG_BASE + 0x3000)
#define USEC_TIME	0x04	/* ST_CLO: lower 32 bits of 64-bit 1 MHz counter */
static uint32_t g_usec_start;

// Buffer for streaming output, and raw Rx data
#define STREAM_BUFFLEN	10000
static char g_stream_buff[STREAM_BUFFLEN];

static struct adc_data g_tmp_data;

static uint32_t *g_rx_buff = g_tmp_data.samples;

// Virtual memory pointers to acceess peripherals & memory
extern MEM_MAP gpio_regs, dma_regs, clk_regs, pwm_regs;
MEM_MAP vc_mem, spi_regs, usec_regs;

// Data formats for -f option
#define FMT_USEC	1

static uint32_t g_samp_total;
static uint32_t g_overrun_total;
static bool g_pwm_debug;		/* set in main(); read by terminate() */

static struct shmem_info g_shm_info;

// Disable SPI
static void spi_disable(void)
{
	*REG32(spi_regs, SPI_CS) = SPI_FIFO_CLR;
	*REG32(spi_regs, SPI_CS) = 0;
}

// Free memory & peripheral mapping and exit
static void terminate(int sig)
{
	printf("Closing\n");
	spi_disable();
	stop_dma(DMA_CHAN_A);
	stop_dma(DMA_CHAN_B);
	stop_dma(DMA_CHAN_C);
	stop_pwm();
	if (g_pwm_debug)
		gpio_mode(PWM_DBG_PIN, GPIO_IN);
	unmap_periph_mem(&vc_mem);
	unmap_periph_mem(&usec_regs);
	unmap_periph_mem(&pwm_regs);
	unmap_periph_mem(&clk_regs);
	unmap_periph_mem(&spi_regs);
	unmap_periph_mem(&dma_regs);
	unmap_periph_mem(&gpio_regs);
	if (g_samp_total)
		printf("Total samples %u, overruns %u\n", g_samp_total, g_overrun_total);

	if (g_shm_info.buff)
		shmem_close(&g_shm_info);

	exit(0);
}

/*
 *  Catastrophic failure in initial setup
 *  TODO: Move to ./rpi_dma_utils.c
 */
void fail(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	terminate(0);
}


// Configure GPIO trigger pins for input
static void gpio_trigger_init(void)
{
	/* Configure trigger pin as input with pull-down resistor
	 * Pull-down ensures stable low state when no signal connected
	 */
	gpio_set(TRIGGER_PIN, GPIO_IN, GPIO_PULLDN);

	fprintf(stderr, "GPIO trigger configured:\n");
	fprintf(stderr, "  GPIO %d (Header Pin 22): input, pull-down, %s\n",
		TRIGGER_PIN, TRIGGER_POLARITY ? "active-high" : "active-low");
}

// Map GPIO, DMA and SPI registers into virtual mem (user space)
// If any of these fail, program will be terminated
static void map_devices(void)
{
	map_periph(&gpio_regs, (void *)GPIO_BASE, PAGE_SIZE);
	map_periph(&dma_regs, (void *)DMA_BASE, PAGE_SIZE);
	map_periph(&spi_regs, (void *)SPI0_BASE, PAGE_SIZE);
	map_periph(&clk_regs, (void *)CLK_BASE, PAGE_SIZE);
	map_periph(&pwm_regs, (void *)PWM_BASE, PAGE_SIZE);
	map_periph(&usec_regs, (void *)USEC_BASE, PAGE_SIZE);
}

// Definitions for SPI frequency test
#define SPI_TEST_TI  (DMA_DEST_DREQ | (DMA_SPI_TX_DREQ << 16) | DMA_WAIT_RESP  | DMA_CB_SRCE_INC)
#define TEST_NSAMPS  10

typedef struct {
	DMA_CB cbs[NUM_CBS];
	uint32_t txd[TEST_NSAMPS], val;
	volatile uint32_t usecs[2];
} TEST_DMA_DATA;

typedef struct {
	DMA_CB cbs[NUM_CBS];
	uint32_t samp_size;
	uint32_t pwm_val;
	uint32_t adc_csd;
	uint32_t txd[2];
	volatile uint32_t usecs[2];
	volatile uint32_t states[2];
	volatile uint32_t rxd1[MAX_SAMPS];
	volatile uint32_t rxd2[MAX_SAMPS];
	volatile uint32_t gpio_rxd1[MAX_SAMPS];  // GPIO samples for buffer 1
	volatile uint32_t gpio_rxd2[MAX_SAMPS];  // GPIO samples for buffer 2
} ADC_DMA_DATA;

// Initialise PWM-paced DMA for ADC sampling
static void adc_dma_init(MEM_MAP *mp, int nsamp, int single,
			 const uint32_t pwm_range, bool pwm_debug)
{
	/*
	 * Debug mode (--pwm-debug) muxes GPIO12 to PWM0_0 (ALT0) so the live
	 * pacing signal is visible on the pin.  No change to clock or range is
	 * needed: with the empirical ×2 factor in pwm_range (see main()) and
	 * PLLD_per ≈ 2×CLOCK_HZ, the actual PWM clock is 2×PWM_FREQ, giving
	 * DATA1/RNG1 = 1/2 = 50% duty cycle at the 1 MHz DREQ rate — a clean
	 * toggling signal with no impact on SPI timing.
	 *
	 * pwm_val = 1 (DATA1) in both modes; DATA1 has no effect on the
	 * DREQ mechanism.
	 */
	const uint32_t pwm_clock    = (uint32_t)PWM_FREQ;
	const uint32_t pwm_range_act = pwm_range;

	ADC_DMA_DATA *dp = mp->virt;
	ADC_DMA_DATA dma_data = {
		.samp_size = 2,
		.pwm_val = 1,		/* DATA1=1; DATA has no effect on DREQ */
		.txd={0xd0, 0xd0}, /* TODO: Use correct MOSI data for BD7910x */
		.adc_csd = SPI_TFR_ACT | SPI_AUTO_CS | SPI_DMA_EN |
			   SPI_FIFO_CLR | ADC_CE_NUM | SPI_CPHA | SPI_CPOL,
		.usecs = {0, 0},
		.states = {0, 0},
		.rxd1 = {0},
		.rxd2 = {0},
		.cbs = {
		// Rx input: read data from usec clock and SPI, into 2 ping-pong buffers
			/*
			 * CB 0 — capture timestamp for ping buffer.
			 * Reads ST_CLO (BCM2711 System Timer, bus addr 0x7E003004)
			 * into usecs[0].  ST_CLO increments at exactly 1 MHz from
			 * the 19.2 MHz crystal oscillator — independent of ARM/GPU
			 * clock scaling.  See USEC_BASE/USEC_TIME comment above and
			 * BCM2711 datasheet Ch. 10, pp. 142–143.
			 */
			{
				.ti = SPI_RX_TI,
//				.ti = NOWAIT_RX_TI,
				.srce_ad = REG(usec_regs, USEC_TIME),
				.dest_ad = MEM(mp, &dp->usecs[0]),
				.tfr_len = 4,
				.stride = 0,
				.next_cb = CBS(1),
				.debug = 0
			}, // 0
			{
				.ti = SPI_RX_TI,
				.srce_ad = REG(spi_regs, SPI_FIFO),
				.dest_ad = MEM(mp, dp->rxd1),
				.tfr_len = nsamp*4,
				.stride = 0,
				.next_cb = CBS(2),
		//		.next_cb = CBS(4),
				.debug = 0
			}, // 1
			{
//				.ti = SPI_RX_TI,
				.ti = NOWAIT_RX_TI,
				.srce_ad = REG(spi_regs, SPI_CS),
				.dest_ad = MEM(mp, &dp->states[0]),
				.tfr_len = 4,
				.stride = 0,
				.next_cb = CBS(3),
//				.next_cb = CBS(4),
				.debug = 0
			}, // 2
			/*
			 * CB 3 — capture timestamp for pong buffer (mirror of CB 0).
			 * Same ST_CLO source; result stored in usecs[1].
			 */
			{
				.ti = SPI_RX_TI,
				.srce_ad = REG(usec_regs, USEC_TIME),
				.dest_ad = MEM(mp, &dp->usecs[1]),
				.tfr_len = 4,
				.stride = 0,
				.next_cb = CBS(4),
				.debug = 0
			}, // 3
			{
				.ti = SPI_RX_TI,
				.srce_ad = REG(spi_regs, SPI_FIFO),
				.dest_ad = MEM(mp, dp->rxd2),
				.tfr_len = nsamp*4,
				.stride = 0,
				.next_cb = CBS(5),
//				.next_cb = CBS(1),
				.debug = 0
			}, // 4
			{
//				.ti = SPI_RX_TI,
				.ti = NOWAIT_RX_TI,
				.srce_ad = REG(spi_regs, SPI_CS),
				.dest_ad = MEM(mp, &dp->states[1]),
				.tfr_len = 4,
				.stride = 0,
				.next_cb = CBS(0),
//				.next_cb = CBS(1),
				.debug = 0
			}, // 5
		// Tx output: 2 data writes to SPI for chan 0 & 1, or both chan 0
			{
				.ti = SPI_TX_TI,
				.srce_ad = MEM(mp, dp->txd),
				.dest_ad = REG(spi_regs, SPI_FIFO),
				.tfr_len = 8,
				.stride = 0,
				.next_cb = CBS(6),
				.debug = 0
			}, // 6
		// PWM ADC trigger: wait for PWM, set sample length, trigger SPI
			/*
			 * CB 7 — pace DMA via PWM DREQ; refill PWM FIFO.
			 *
			 * TI flags (PWM_TI):
			 *   DEST_DREQ (bit 6)  : destination writes are gated by DREQ.
			 *   PERMAP = 5         : selects PWM0 DREQ (BCM2711 Table 53,
			 *                        §4.2.1.3 p.61; DMA_PWM_DREQ = 5).
			 *   WAIT_RESP          : DMA waits for write ACK before next CB.
			 *
			 * How pacing works:
			 *   The PWM DMAC register has DREQ threshold = 1 (set via
			 *   PWM_DMAC = PWM_DMAC_ENAB | PWM_ENAB = 0x80000001,
			 *   BCM2711 Table 155, §8.6 p.131).  The DREQ line is
			 *   asserted (high = "I need data") whenever the 64-word
			 *   FIFO drops below this threshold, i.e. when it is empty.
			 *   Because DREQ is level-sensitive (§4.2.1.3, p.61), DMA
			 *   channel A stalls here until DREQ is active.
			 *
			 *   This CB writes pwm_val (= 1, DATA1) to PWM_FIF1
			 *   (BCM2711 §8.6 Table 158, p.132 — write-only FIFO
			 *   input register at PWM base + 0x18).  One word enters
			 *   the FIFO, DREQ de-asserts (FIFO no longer empty).
			 *
			 *   The PWM channel (configured in serialiser mode with
			 *   USEF1=1, PWEN1=1 via start_pwm()) consumes that word
			 *   in exactly pwm_range PWM-clock cycles (= pwm_range µs
			 *   at PWM_FREQ=1 MHz).  After pwm_range µs the FIFO is
			 *   empty again and DREQ re-asserts for the next cycle.
			 *
			 * Trigger rate derivation:
			 *   f_trigger = pwm_clock / pwm_range_act
			 *             = PWM_FREQ / pwm_range
			 *             = 1,000,000 / 2 = 500,000 Hz (nominal)
			 *   With PLLD_per ≈ 2×CLOCK_HZ: actual PWM clock ≈ 2×PWM_FREQ,
			 *   so f_trigger = 2×PWM_FREQ / 2 = 1,000,000 Hz = 1 MSPS.
			 *   (See pwm_range empirical-factor-of-2 note in main().)
			 *   --pwm-debug uses the same clock/range: GPIO12 shows a
			 *   ~50% duty cycle square wave at the same 1 MHz rate.
			 *
			 * This CB executes once per PWM period, setting the pace
			 * for CBs 8 and 9 which execute in the SAME DREQ window.
			 */
			{
				.ti = PWM_TI,
				.srce_ad = MEM(mp, &dp->pwm_val),
				.dest_ad = REG(pwm_regs, PWM_FIF1),
				.tfr_len = 4,
				.stride = 0,
				.next_cb = CBS(8),
				.debug = 0
			}, // 7
			/*
			 * CB 8 — set SPI transfer length.
			 *
			 * Writes samp_size (= 2, i.e. 2 bytes = 16 bits) to
			 * SPI_DLEN (BCM2711 §10 SPI, SPI0 register map, offset
			 * 0x0C).  This programs the SPI peripheral for exactly
			 * one 16-bit ADC sample per triggered transaction.
			 *
			 * DREQ is still asserted here: the BCM2711 PWM DMAC DREQ
			 * threshold semantics are "level ≤ threshold" (§8.6
			 * Table 155, p.131: "...whenever the FIFO level is less
			 * than or equal to this value").  After the PREVIOUS
			 * period's CB 7 wrote one word, level = 1 ≤ threshold = 1,
			 * so DREQ stayed asserted.  CB 7 then stalled after its
			 * write (next CB 8 blocked).  When PWM reads that word at
			 * the start of the new period the level drops back toward
			 * 1, keeping DREQ asserted.  CB 8 writes to SPI_DLEN —
			 * not to the PWM FIFO — so the FIFO level and DREQ remain
			 * unchanged.
			 */
			{
				.ti = PWM_TI,
				.srce_ad = MEM(mp, &dp->samp_size),
				.dest_ad = REG(spi_regs, SPI_DLEN),
				.tfr_len = 4,
				.stride = 0,
				.next_cb = CBS(9),
				.debug = 0
			}, // 8
			/*
			 * CB 9 — trigger SPI transaction.
			 *
			 * Writes adc_csd (SPI_TFR_ACT | SPI_AUTO_CS | SPI_DMA_EN
			 * | SPI_FIFO_CLR | ...) to SPI_CS (BCM2711 §10, offset
			 * 0x00), which sets the TA (Transfer Active) bit and
			 * starts a new SPI DMA transaction of SPI_DLEN = 2 bytes.
			 *
			 * After this write:
			 *   - The SPI peripheral begins clocking out 16 bits on
			 *     MOSI while capturing 16 bits on MISO from the ADC.
			 *     At 20 MHz SPI clock the transaction takes 800 ns.
			 *   - SPI RX DMA (channel B, CB 0-5) reads the received
			 *     word from SPI_FIFO into the ping-pong data buffer
			 *     using SPI_RX_DREQ flow control.
			 *
			 * DREQ is still asserted here (same reasoning as CB 8 —
			 * no write to the PWM FIFO since the start of this DREQ
			 * window).  After CB 9 the chain loops to CB 7.
			 *
			 * HOW THE DREQ-PACING CHAIN ACTUALLY OPERATES
			 * ============================================
			 * BCM2711 §8.6 p.131 (Table 155, DREQ field): "The FIFO
			 * source will signal DMA DREQ whenever the FIFO level is
			 * less than or equal to this value."  With threshold = 1:
			 *   DREQ asserted  when level ≤ 1 (0 or 1 words in FIFO)
			 *   DREQ de-asserted when level = 2 (after CB 7 writes)
			 *
			 * Steady-state FIFO level oscillates 2→1→2→1…:
			 *   PWM reads one word per period (level 2→1) → DREQ fires
			 *   CB 8→9→7 execute: SPI triggered, FIFO refilled (1→2)
			 *   → DREQ de-asserts; CB 8 stalls until next period
			 *
			 * KEY POINT: in steady state the DMA runs CB 8→CB 9→CB 7
			 * each period (not CB 7→8→9 as the chain order suggests).
			 * CB 7 ran last time and stalled CB 8; the current DREQ
			 * event first releases CB 8, then CB 9, then CB 7.
			 * The SPI transfer is therefore triggered (CB 9) BEFORE
			 * the FIFO is refilled for the new period (CB 7), giving
			 * the full period duration for the SPI to complete.
			 *
			 * One complete sample cycle (actual PWM clock ≈ 2 MHz,
			 * RNG = 2, period = 1 µs; see pwm_range note in main()):
			 *   t = 0        PWM reads word; level 2→1; DREQ fires
			 *   t = 0+ε      CB 8 sets SPI_DLEN  (no FIFO write; DREQ stays on)
			 *   t = 0+2ε     CB 9 writes SPI_CS → SPI starts (800 ns)
			 *   t = 0+3ε     CB 7 writes pwm_val; level 1→2; DREQ off; CB 8 stalls
			 *   t = 0–0.5µs  PWM HIGH (DATA=1 of RNG=2 ticks at 2 MHz)
			 *   t = 0.5–1µs  PWM LOW
			 *   t ≈ 800 ns   SPI complete; RX DMA stores sample
			 *   t = 1 µs     Next period: level 2→1; DREQ fires; cycle repeats
			 *   Sample rate = 1 / 1 µs = 1,000,000 samples/sec
			 */
			{
				.ti = PWM_TI,
				.srce_ad = MEM(mp, &dp->adc_csd),
				.dest_ad = REG(spi_regs, SPI_CS),
				.tfr_len = 4,
				.stride = 0,
				.next_cb = CBS(7),
				.debug = 0
			}, // 9
		}
	};

	if (single)								 // If single-shot, stop after first Rx block
		dma_data.cbs[2].next_cb = 0;
	memcpy(dp, &dma_data, sizeof(dma_data));	// Copy DMA data into uncached memory
	init_pwm(pwm_clock, pwm_range_act, PWM_VALUE);
	/*
	 * GPIO12 debug mux (--pwm-debug) is applied in adc_stream_start(),
	 * AFTER start_pwm(), to avoid a spurious HIGH on the pin while the
	 * PWM peripheral is idle (BCM2711 PWM output idles HIGH when disabled).
	 */
	*REG32(pwm_regs, PWM_DMAC) = PWM_DMAC_ENAB | PWM_ENAB;
	*REG32(spi_regs, SPI_DC) = SPI_DMA_PRIORITY;			// Set DMA priorities
	*REG32(spi_regs, SPI_CS) = SPI_FIFO_CLR;					// Clear SPI FIFOs
	start_dma(mp, DMA_CHAN_C, &dp->cbs[6], 0);  // Start SPI Tx DMA
	start_dma(mp, DMA_CHAN_B, &dp->cbs[0], 0);  // Start SPI Rx DMA
	start_dma(mp, DMA_CHAN_A, &dp->cbs[7], 0);  // Start PWM DMA, for SPI trigger
}

// Start ADC data acquisition
static void adc_stream_start(void)
{
	start_pwm();
	if (g_pwm_debug) {
		/*
		 * Mux GPIO12 to PWM0_0 (ALT0) AFTER start_pwm() so the pin is
		 * only connected to the PWM output once the PWM is already
		 * running.  If muxed while PWM is disabled (PWM_CTL=0), the
		 * BCM2711 PWM output idles HIGH, causing a spurious initial HIGH
		 * on the debug pin for the duration between gpio_mode and
		 * start_pwm.  GPIO12 ALT0 = PWM0_0: BCM2711 §5, p.92.
		 */
		gpio_mode(PWM_DBG_PIN, GPIO_ALT0);
		printf("PWM debug output enabled on GPIO%d (header pin 32): "
		       "~50%% duty cycle at 1 MHz\n", PWM_DBG_PIN);
	}
}

#define NUMDBG 0xff
uint32_t g_dbg_first[NUMDBG];
uint32_t g_dbg_last[NUMDBG];

uint32_t swap32(uint32_t orig)
{
  return ((orig & 0xFF000000) >> 24) | ((orig & 0x00FF0000) >> 8) |
         ((orig & 0x0000FF00) << 8) | ((orig & 0x000000FF) << 24);
}

static int adc_stream_csv(MEM_MAP *mp, char *vals, int maxlen, int nsamp, struct mvaring *mr)
{
	ADC_DMA_DATA *dp=mp->virt;
	uint32_t /*i,*/ n, usec, slen=0;
	static int numdbg = 0;

	for (n=0; n<2 && slen==0; n++)
	{
		if (dp->states[n])
		{
			g_samp_total += nsamp;
			/* Copy ADC and GPIO data to adc_data struct */
			memcpy(g_rx_buff, n ? (void *)dp->rxd2 : (void *)dp->rxd1, nsamp*4);
			if (numdbg < NUMDBG) {

				g_dbg_first[numdbg] = g_rx_buff[0];
				g_dbg_last[numdbg] = g_rx_buff[ARRAY_SIZE(g_tmp_data.samples) - 1];

				numdbg ++;
			}

			memcpy(g_tmp_data.gpio_lev0, n ? (void *)dp->gpio_rxd2 : (void *)dp->gpio_rxd1, nsamp*4);
			usec = dp->usecs[n];
			if (dp->states[n^1])
			{
				dp->states[0] = dp->states[1] = 0;
				g_overrun_total++;
				break;
			}
			dp->states[n] = 0;
			if (g_usec_start == 0)
				g_usec_start = usec;

			/* 32bit counter lasts around 71 minutes until wrapping */
			g_tmp_data.usecs = usec-g_usec_start;

			/* When ring is full, stop ADC but keep shared memory alive for consumers */
			if (ring_add(mr, &g_tmp_data, true)) {
				char cmd[32];
				int i;

				for (i = 1; i < numdbg; i++) {
					uint32_t first, last;

					first = swap32(g_dbg_first[i]) >> 16;
					last = swap32(g_dbg_last[i - 1]) >> 16;

					printf("[l%u f%u]: 0x%x 0x%x (diff %u)\n", i - 1, i, last, first, first -  last);
				}

				printf("\noverrun: %u\n", g_overrun_total);
				printf("\nRing buffer full, stopping ADC capture\n");
				printf("Shared memory preserved for consumers to drain buffer.\n");
				printf("Type 'quit' or 'q' and press Enter to exit: ");
				
				while (fgets(cmd, sizeof(cmd), stdin)) {
					if (strncmp(cmd, "quit", 4) == 0 || cmd[0] == 'q') {
						printf("Exiting...\n");
						terminate(0);
					}
					printf("Type 'quit' or 'q' and press Enter to exit: ");
				}
			}
		}
	}
	vals[slen] = 0;

	return(slen);
}

// Fetch samples from ADC buffer, return comma-delimited integer values
// Test of SPI write cycles
// Redundant code, kept in as an explanation of SPI data length
int spi_tx_test(MEM_MAP *mp, uint16_t *buff, int nsamp)
{
	uint32_t n, a=0;

	nsamp = 8;
	*REG32(spi_regs, SPI_CS) = SPI_FIFO_CLR;
#if 1
	// Write data length to DLEN reg (with ACT clear)
	for (n=0; n<nsamp; n++)
	{
		*REG32(spi_regs, SPI_DLEN) = 2;
		*REG32(spi_regs, SPI_CS) = SPI_TFR_ACT | SPI_AUTO_CS | SPI_DMA_EN | SPI_FIFO_CLR | SPI_CPHA | SPI_CPOL;
		*REG32(spi_regs, SPI_FIFO) = n;
		usleep(5);
		a += *REG32(spi_regs, SPI_FIFO);
		// *REG32(spi_regs, SPI_CS) = SPI_FIFO_CLR; // Not needed, as ACT is already clear
	}
#else
	// Write data length to FIFO (with ACT set)
	*REG32(spi_regs, SPI_CS) = SPI_TFR_ACT | SPI_AUTO_CS | SPI_DMA_EN | SPI_FIFO_CLR;
	for (n=0; n<nsamp; n++)
	{
		*REG32(spi_regs, SPI_FIFO) = (2<<16) | SPI_TFR_ACT | SPI_FIFO_CLR;
		*REG32(spi_regs, SPI_FIFO) = n;
		usleep(5);
		a += *REG32(spi_regs, SPI_FIFO);
		// *REG32(spi_regs, SPI_CS) = SPI_FIFO_CLR; // Clearing ACT would disrupt comms
	}
#endif
	return(0);
}

// Initialise SPI0, given desired clock freq; return actual value
static int init_spi(int hz)
{
	int f, div = (SPI_CLOCK_HZ / hz + 1) & ~1;

	gpio_set(SPI0_CE0_PIN, GPIO_ALT0, GPIO_NOPULL);
	gpio_set(SPI0_CE1_PIN, GPIO_ALT0, GPIO_NOPULL);
	gpio_set(SPI0_MISO_PIN, GPIO_ALT0, GPIO_PULLUP);
	gpio_set(SPI0_MOSI_PIN, GPIO_ALT0, GPIO_NOPULL);
	gpio_set(SPI0_SCLK_PIN, GPIO_ALT0, GPIO_NOPULL);
	while (div==0 || (f = SPI_CLOCK_HZ/div) > MAX_SPI_FREQ)
		div += 2;
	*REG32(spi_regs, SPI_CS) = 0x30;
	*REG32(spi_regs, SPI_CLK) = div;
	return(f);
}

static void print_usage(const char *prog_name)
{
	printf("Usage: %s [options]\n", prog_name);
	printf("Start reading ADC data\n\n");
	printf("Options:\n");
	printf("  -c  --create-shm       Create the shared-memory buffer and start reading\n");
	printf("  -d, --pwm-debug        Output PWM pacing signal on GPIO12 (header pin 32)\n");
	printf("                         GPIO12 ALT0 = PWM0_0; zero DMA overhead.\n");
	printf("  -r, --realtime-sched   Use SCHED_FIFO for real-time scheduling\n");
	printf("  -h, --help             Show this help message\n\n");
	printf("Output data to 'mvaring' type ring buffer in shared memory\n");
	printf("Raw shared memory is in '/dev/shm%s'\n", SHM_NAME);
	printf("See the data-format from mvaring.h\n");
}

int shm_try_open(struct shmem_info *shi, struct mvaring **mr)
{
	int ret;

	/*
	 * Try opening until it succeeds
	 * TODO: Add a time-out.
	 */
	do {
		ret = shmem_open(SHM_NAME, SHM_SIZE, shi, true);
		if (ret) {
			if (ret != -ENOENT) {
				printf("Nooo\n");

				return ret;
			}
			sleep(0);
		}
	} while (ret);

	*mr = shi->buff;

	while (!ring_is_ok(*mr))
		sleep(0);

	return 0;
}

#define ADC_SCHED_PRIO 10

int set_sched()
{
	struct sched_param param;
	int policy = SCHED_FIFO;
	int ret;

	param.sched_priority = ADC_SCHED_PRIO;
	if (sched_setscheduler(0, policy, &param) == -1) {
		ret = errno;
		printf("sched_setscheduler failed: %s\n", strerror(errno));

		return ret;
	}

	return 0;
}

// Main program
int main(int argc, char *argv[])
{
	/*
	 * PWM pacing rate calculation
	 * ============================
	 * The PWM peripheral is used as a precise timing source that paces the
	 * DMA engine, which in turn triggers one SPI transaction (= one ADC
	 * sample) per PWM period.
	 *
	 * init_pwm() configures the BCM2711 Clock Manager to divide the PLLD_per
	 * source (nominally CLOCK_HZ = 375 MHz, clock manager source SRC=6,
	 * BCM2711 datasheet §9 "Clock Manager", p.105, Table 94) by
	 * (CLOCK_HZ / PWM_FREQ) = 375, yielding a nominal PWM clock of PWM_FREQ.
	 * The actual PLLD_per frequency is firmware-dependent — see the
	 * "empirical factor of 2" note in the CURRENT CONFIGURATION block below.
	 *
	 * The PWM RNG1 register is loaded with pwm_range (BCM2711 datasheet
	 * §8 "PWM", Table 156 RNG1/RNG2, p.132).  The PWM channel consumes
	 * one FIFO word every pwm_range clock cycles, i.e. every:
	 *
	 *   pwm_period = pwm_range / PWM_FREQ  seconds
	 *
	 * Each time the FIFO drains below the DREQ threshold (set to 1 by
	 * PWM_DMAC = PWM_DMAC_ENAB | PWM_ENAB, Table 155 DMAC p.131), the
	 * PWM asserts its level-sensitive DREQ line (BCM2711 §4.2.1.3
	 * "Peripheral DREQ Signals", p.61).  DMA channel A (PWM_TI =
	 * DMA_DEST_DREQ | PERMAP=5) is stalled until DREQ is active, so it
	 * fires once per pwm_period.  CB 9 writes SPI_CS to trigger one SPI
	 * transaction per DREQ, giving a sample rate of:
	 *
	 *   sample_rate = PWM_FREQ / pwm_range
	 *
	 * CURRENT CONFIGURATION (HI_SPEED, SAMPLE_RATE = 1,000,000):
	 *   pwm_range = (PWM_FREQ * 2) / SAMPLE_RATE = 2
	 *
	 * NOTE — empirical factor of 2:
	 *   The factor of 2 in pwm_range is empirically required to achieve the
	 *   correct sample rate.  The root cause is not fully understood, but the
	 *   most consistent explanation is that CLOCK_HZ (375 MHz as defined in
	 *   rpi_dma_utils.h) does not match the actual PLLD_per frequency on this
	 *   RPi 4.  Recent RPi OS firmware is known to run PLLD_per at 750 MHz.
	 *
	 *   When using --pwm-debug (GPIO12 output), the observed GPIO12 frequency
	 *   is 2× the value computed from CLOCK_HZ = 375 MHz, which is consistent
	 *   with PLLD_per = 750 MHz:
	 *
	 *     actual PWM clock = 750 MHz / (375 MHz / PWM_FREQ) = 2 × PWM_FREQ
	 *     trigger rate     = 2 × PWM_FREQ / pwm_range
	 *                      = 2 × PWM_FREQ / 2 = PWM_FREQ = 1 MHz  → 1 MSPS
	 *
	 *   Without the factor of 2 (pwm_range=1), the trigger rate would be
	 *   2 MSPS, which is beyond what the SPI/DMA pipeline can sustain.
	 *
	 *   Until the actual PLLD_per is verified on the target hardware
	 *   (e.g. via 'vcgencmd measure_clock pwm' with DMA running, or by
	 *   directly reading the CM_PWMDIV register), do NOT remove this
	 *   factor of 2 and do NOT change CLOCK_HZ without measurement.
	 *
	 * Note on PWM clock stability: the PWM clock is derived from PLLD_per
	 * via the clock manager (not directly from xosc like the System Timer).
	 * PLLD is a PLL seeded from the 19.2 MHz crystal oscillator and its
	 * frequency is set at boot by the firmware.  Unlike the ARM core PLL
	 * (which DVFS adjusts), PLLD is NOT changed dynamically in normal
	 * operation, so the PWM period is stable at runtime.
	 */
	const uint32_t pwm_range = (PWM_FREQ * 2) / SAMPLE_RATE;
	static struct option long_options[] = {
		{"realtime-sched",	no_argument,		NULL, 'r'},
		{"create-shm",		no_argument,		NULL, 'c'},
		{"pwm-debug",		no_argument,		NULL, 'd'},
		{"help",		no_argument,		NULL, 'h'},
		{NULL,			0,			NULL, 0}
	};
	struct mvaring *mr;
	int f, ret, opt;
	bool create_shm = false, sched_fifo = false, pwm_debug = false;

	printf("RPi ADC streamer v" VERSION "\n");

	/* Parse command line arguments */
	while ((opt = getopt_long(argc, argv, "cdhr", long_options, NULL)) != -1) {
		switch (opt) {
		case 'c':
			create_shm = true;
			break;
		case 'd':
			pwm_debug = true;
			break;
		case 'h':
			print_usage(argv[0]);
			return 0;
		case 'r':
			sched_fifo = true;
			break;
		default:
			fprintf(stderr, "Use -h for help\n");
			return 1;
		}
	}

	if (create_shm)
		ret = rpi_shm_create(&g_shm_info, &mr);
	else
		ret = shm_try_open(&g_shm_info, &mr);

	if (ret)
		return ret;

	if (sched_fifo)
		ret = set_sched();

	if (ret)
		return ret;

	map_devices();
	gpio_trigger_init();
	map_uncached_mem(&vc_mem, VC_MEM_SIZE);
	signal(SIGINT, terminate);
	f = init_spi(SPI_FREQ);

	printf("Streaming %u samples per block at %llu S/s, freq %d\n",
	       MAX_SAMPS, SAMPLE_RATE, f);
	g_pwm_debug = pwm_debug;
	adc_dma_init(&vc_mem, MAX_SAMPS, 0, pwm_range, pwm_debug);
	adc_stream_start();
	while (1)
		adc_stream_csv(&vc_mem, g_stream_buff, STREAM_BUFFLEN, MAX_SAMPS, mr);

	terminate(0);
}

