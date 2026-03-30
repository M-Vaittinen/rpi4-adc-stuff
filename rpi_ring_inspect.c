/*
 * rpi_ring_inspect.c - Interactive ring buffer file inspection tool
 *
 * Opens a binary file containing an mvaring ring buffer snapshot, validates
 * it, then provides a split-screen ncurses shell for analysis commands.
 *
 * Screen layout:
 *   +-------------------------------+
 *   |  output / results area        |  (scrollable)
 *   +-------------------------------+
 *   |  status bar                   |
 *   +-------------------------------+
 *   |  command input  >_            |
 *   +-------------------------------+
 */

#define _GNU_SOURCE
#include <curses.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"
#include "mvaring.h"

/* ------------------------------------------------------------------ */
/* Constants                                                            */
/* ------------------------------------------------------------------ */

#define CMD_MAX_LEN	256
#define CMD_HISTORY_SZ	32
#define OUT_BUF_LINES	4096	/* scrollback line count */
#define OUT_LINE_LEN	512	/* max chars per output line */
#define INPUT_ROWS	2	/* rows reserved for input (below separator) */
#define MOUSE_SCROLL_LINES 3	/* output lines scrolled per mouse-wheel notch */

/* ------------------------------------------------------------------ */
/* Forward declarations                                                 */
/* ------------------------------------------------------------------ */

static void cmd_ts_stats(const char *args);
static void cmd_info(const char *args);
static void cmd_inspect(const char *args);
static void cmd_check_pattern(const char *args);
static void cmd_help(const char *args);
static void cmd_quit(const char *args);

/* ------------------------------------------------------------------ */
/* Command dispatch table                                               */
/*                                                                      */
/* To add a new command:                                                */
/*   1. Write a handler: static void cmd_foo(const char *args) { ... } */
/*   2. Add an entry below: { "foo", cmd_foo, "description" }          */
/* ------------------------------------------------------------------ */

struct cmd_entry {
	const char *name;
	void (*handler)(const char *args);
	const char *help;
};

static const struct cmd_entry g_commands[] = {
	{ "ts_stats",      cmd_ts_stats,
	  "Avg/stddev/min/max of consecutive chunk timestamp intervals" },
	{ "info",          cmd_info,
	  "Show ring buffer metadata (version, indices, fill level)" },
	{ "inspect",       cmd_inspect,
	  "Show sample data: inspect chunk=N sample=M [num=K]" },
	{ "check_pattern", cmd_check_pattern,
	  "Verify test-slave counter data: check_pattern [nostart] [bits=N]" },
	{ "help",          cmd_help,
	  "List available commands" },
	{ "quit",          cmd_quit,
	  "Exit the program" },
	{ "q",             cmd_quit,
	  "Exit the program (alias for quit)" },
	{ NULL, NULL, NULL }  /* sentinel */
};

/* ------------------------------------------------------------------ */
/* Global state                                                         */
/* ------------------------------------------------------------------ */

static struct mvaring	*g_ring;
static size_t		 g_file_size;
static const char	*g_filename;

/* ncurses windows */
static WINDOW *g_out_win;	/* upper scrollable output */
static WINDOW *g_sep_win;	/* single-row separator / status bar */
static WINDOW *g_inp_win;	/* lower command input area */

/* Output scrollback ring */
static char	g_out_buf[OUT_BUF_LINES][OUT_LINE_LEN];
static int	g_out_total;	/* total lines ever appended (monotonic) */
static int	g_out_scroll;	/* lines scrolled back from bottom (0=latest) */

static bool	g_running = true;

/* ------------------------------------------------------------------ */
/* Output helpers                                                       */
/* ------------------------------------------------------------------ */

static void out_print(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void out_print(const char *fmt, ...)
{
	va_list ap;
	int slot = g_out_total % OUT_BUF_LINES;

	va_start(ap, fmt);
	vsnprintf(g_out_buf[slot], OUT_LINE_LEN, fmt, ap);
	va_end(ap);
	g_out_total++;
}

static void out_refresh(void)
{
	int rows, cols;

	getmaxyx(g_out_win, rows, cols);
	(void)cols;
	wclear(g_out_win);

	/* Oldest line still in the ring */
	int oldest = (g_out_total > OUT_BUF_LINES)
		     ? (g_out_total - OUT_BUF_LINES) : 0;

	/* First line index to display, considering scroll */
	int first = g_out_total - rows - g_out_scroll;
	if (first < oldest) {
		first = oldest;
		/* Clamp scroll to prevent scrolling past oldest */
		g_out_scroll = g_out_total - rows - oldest;
		if (g_out_scroll < 0)
			g_out_scroll = 0;
	}

	int row = 0;

	for (int ln = first; ln < first + rows && ln < g_out_total; ln++, row++)
		mvwprintw(g_out_win, row, 0, "%s", g_out_buf[ln % OUT_BUF_LINES]);

	wrefresh(g_out_win);
}

/* ------------------------------------------------------------------ */
/* Status bar                                                           */
/* ------------------------------------------------------------------ */

static void sep_refresh(void)
{
	int cols = getmaxx(g_sep_win);
	unsigned w = atomic_load_explicit(&g_ring->windex, memory_order_relaxed);
	unsigned rd = atomic_load_explicit(&g_ring->rindex, memory_order_relaxed);
	unsigned avail = w - rd;

	if (avail > NUM_DATA_CHUNKS)
		avail = NUM_DATA_CHUNKS;

	char status[OUT_LINE_LEN];
	int n = snprintf(status, sizeof(status),
			 " %s  v%u | %u/%u chunks",
			 g_filename, g_ring->version, avail, NUM_DATA_CHUNKS);

	if (g_out_scroll > 0 && n < (int)sizeof(status) - 1)
		snprintf(status + n, sizeof(status) - n,
			 "  [scrolled +%d]", g_out_scroll);

	wattron(g_sep_win, A_REVERSE);
	mvwprintw(g_sep_win, 0, 0, "%-*s", cols, status);
	wattroff(g_sep_win, A_REVERSE);
	wrefresh(g_sep_win);
}

/* ------------------------------------------------------------------ */
/* Ring buffer helpers                                                  */
/* ------------------------------------------------------------------ */

/* Number of valid entries currently in the ring. */
static unsigned int ring_valid_count(void)
{
	unsigned w = atomic_load_explicit(&g_ring->windex, memory_order_relaxed);
	unsigned rd = atomic_load_explicit(&g_ring->rindex, memory_order_relaxed);
	unsigned avail = w - rd;

	return avail < NUM_DATA_CHUNKS ? avail : NUM_DATA_CHUNKS;
}

/*
 * Logical chunk index i (0 = oldest, ring_valid_count()-1 = newest).
 * Call ring_valid_count() first to know the valid range.
 */
static inline const struct adc_data *ring_chunk_at(unsigned oldest_abs,
						    unsigned i)
{
	return &g_ring->buf[(oldest_abs + i) & BUFF_MASK];
}

/* ------------------------------------------------------------------ */
/* Commands                                                             */
/* ------------------------------------------------------------------ */

static void cmd_info(const char *args)
{
	(void)args;

	unsigned w  = atomic_load_explicit(&g_ring->windex, memory_order_relaxed);
	unsigned rd = atomic_load_explicit(&g_ring->rindex, memory_order_relaxed);
	unsigned avail = ring_valid_count();

	out_print("--- Ring buffer info ---");
	out_print("  Version        : %u  (expected %u)",
		  g_ring->version, MVARING_VERSION);
	out_print("  Struct size    : %u bytes  (sizeof: %zu)",
		  g_ring->size, sizeof(struct mvaring));
	out_print("  File size      : %zu bytes", g_file_size);
	out_print("  windex         : %u", w);
	out_print("  rindex         : %u", rd);
	out_print("  Available      : %u / %u chunks  (%u samples each)",
		  avail, NUM_DATA_CHUNKS, MAX_SAMPS);
	out_print("  Dropped        : %u  (overwritten by slow reader)",
		  g_ring->dropped);
}

/*
 * cmd_ts_stats - analyse timestamp deltas between consecutive chunks.
 *
 * Each adc_data chunk carries a 'usecs' timestamp (microseconds since boot).
 * We compute the interval between consecutive chunks, then report:
 *   - average interval and implied sample rate
 *   - standard deviation of the interval (jitter metric)
 *   - minimum/maximum interval and their deviation from average
 */
static void cmd_ts_stats(const char *args)
{
	(void)args;

	unsigned w  = atomic_load_explicit(&g_ring->windex, memory_order_relaxed);
	unsigned rd = atomic_load_explicit(&g_ring->rindex, memory_order_relaxed);
	unsigned avail = w - rd;

	if (avail > NUM_DATA_CHUNKS)
		avail = NUM_DATA_CHUNKS;

	if (avail < 2) {
		out_print("ts_stats: need >= 2 chunks in buffer (have %u)", avail);
		return;
	}

	unsigned oldest = w - avail;	/* absolute index of oldest entry */
	unsigned count  = avail - 1;	/* number of consecutive pairs */

	double sum = 0.0;
	double min_diff =  1e18;
	double max_diff = -1e18;

	/* First pass: avg and min/max */
	for (unsigned i = 0; i < count; i++) {
		const struct adc_data *a = ring_chunk_at(oldest, i);
		const struct adc_data *b = ring_chunk_at(oldest, i + 1);
		/*
		 * uint32_t usecs wraps at ~4295 s. For typical recordings
		 * (< 1 s at 1 MSPS / 8192 chunks) wrap is not an issue.
		 * Cast difference to int32_t to handle the rare wrap case.
		 */
		double diff = (double)(int32_t)(b->usecs - a->usecs);

		sum += diff;
		if (diff < min_diff) min_diff = diff;
		if (diff > max_diff) max_diff = diff;
	}

	double avg = sum / count;

	/* Second pass: std deviation and signed deviations from avg */
	double max_dev = -1e18;
	double min_dev =  1e18;
	double sum_sq  = 0.0;

	for (unsigned i = 0; i < count; i++) {
		const struct adc_data *a = ring_chunk_at(oldest, i);
		const struct adc_data *b = ring_chunk_at(oldest, i + 1);
		double diff = (double)(int32_t)(b->usecs - a->usecs);
		double dev  = diff - avg;

		sum_sq += dev * dev;
		if (dev > max_dev) max_dev = dev;
		if (dev < min_dev) min_dev = dev;
	}

	double stddev = sqrt(sum_sq / count);
	double ksps = (avg > 0.0) ? (double)MAX_SAMPS / avg * 1000.0 : 0.0;

	out_print("--- Timestamp statistics (%u pairs from %u chunks) ---", count, avail);
	out_print("  Avg chunk interval : %8.3f us  =>  %.1f kSPS",
		  avg, ksps);
	out_print("  Std deviation      : %8.3f us  (%.3f %% of avg)",
		  stddev, avg > 0.0 ? 100.0 * stddev / avg : 0.0);
	out_print("  Min interval       : %8.3f us  (dev: %+.3f us, %+.2f %%)",
		  min_diff, min_diff - avg,
		  avg > 0.0 ? 100.0 * (min_diff - avg) / avg : 0.0);
	out_print("  Max interval       : %8.3f us  (dev: %+.3f us, %+.2f %%)",
		  max_diff, max_diff - avg,
		  avg > 0.0 ? 100.0 * (max_diff - avg) / avg : 0.0);
	out_print("  Jitter range       : %8.3f us  (max - min)",
		  max_diff - min_diff);
}

/*
 * extract_test_val - recover the 16-bit test-slave counter value from a
 * raw 32-bit DMA RX FIFO word.
 *
 * The BCM2711 SPI RX FIFO stores the first received byte (SPI MSB) in
 * bits[7:0] and the second byte (SPI LSB) in bits[15:8].  Byte-swapping
 * the lower 16 bits reconstructs the original 16-bit slave value.
 */
static inline uint16_t extract_test_val(uint32_t raw)
{
	uint16_t w = (uint16_t)raw;

	return (uint16_t)((w << 8) | (w >> 8));
}

/*
 * cmd_inspect - show decoded sample data for a range of samples.
 *
 * Usage: inspect chunk=N sample=M [num=K]
 *
 *   chunk   logical chunk index (0 = oldest chunk in the ring)
 *   sample  sample index within the chunk (0 … MAX_SAMPS-1)
 *   num     number of consecutive samples to display (default 1)
 *           the range may span chunk boundaries
 *
 * For each sample the output shows:
 *   chunk / sample index, chunk timestamp (µs), raw 32-bit FIFO word,
 *   decoded 16-bit value (byte-swapped lower half), and the upper 16
 *   bits of the FIFO word (must be 0 for normal 16-bit SPI transfers).
 */
static void cmd_inspect(const char *args)
{
	long chunk_idx = -1, sample_idx = -1, num = 1;
	bool chunk_set = false, sample_set = false;

	/* Parse key=value tokens */
	if (args) {
		const char *p = args;

		while (*p) {
			while (*p == ' ') p++;
			if (!*p) break;

			char *end;

			if (strncmp(p, "chunk=", 6) == 0) {
				chunk_idx = strtol(p + 6, &end, 10);
				chunk_set = true;
				p = end;
			} else if (strncmp(p, "sample=", 7) == 0) {
				sample_idx = strtol(p + 7, &end, 10);
				sample_set = true;
				p = end;
			} else if (strncmp(p, "num=", 4) == 0) {
				num = strtol(p + 4, &end, 10);
				p = end;
			} else {
				/* Skip unrecognised token */
				while (*p && *p != ' ') p++;
			}
		}
	}

	if (!chunk_set || !sample_set) {
		out_print("inspect: usage: inspect chunk=N sample=M [num=K]");
		return;
	}

	if (num < 1) {
		out_print("inspect: num must be >= 1");
		return;
	}

	unsigned w     = atomic_load_explicit(&g_ring->windex, memory_order_relaxed);
	unsigned rd    = atomic_load_explicit(&g_ring->rindex, memory_order_relaxed);
	unsigned avail = w - rd;

	if (avail > NUM_DATA_CHUNKS)
		avail = NUM_DATA_CHUNKS;

	if (avail == 0) {
		out_print("inspect: ring is empty");
		return;
	}

	if (chunk_idx < 0 || (unsigned long)chunk_idx >= avail) {
		out_print("inspect: chunk %ld out of range (0 – %u)",
			  chunk_idx, avail - 1);
		return;
	}

	if (sample_idx < 0 || sample_idx >= MAX_SAMPS) {
		out_print("inspect: sample %ld out of range (0 – %d)",
			  sample_idx, MAX_SAMPS - 1);
		return;
	}

	unsigned oldest = w - avail;

	out_print("chunk   samp  timestamp(us)  raw_32bit   decoded  hi16");
	out_print("------  ----  -------------  ----------  -------  ----");

	long shown = 0;
	long ci = chunk_idx;
	long si = sample_idx;

	while (shown < num) {
		if (ci >= (long)avail)
			break;

		const struct adc_data *chunk = ring_chunk_at(oldest, (unsigned)ci);

		for (; si < MAX_SAMPS && shown < num; si++, shown++) {
			uint32_t raw = chunk->samples[si];
			uint16_t decoded = extract_test_val(raw);
			uint16_t hi16    = (uint16_t)(raw >> 16);

			out_print("%6ld  %4ld  %13u  0x%08X  0x%04X   0x%04X",
				  ci, si, chunk->usecs, raw, decoded, hi16);
		}

		ci++;
		si = 0;
	}

	if (shown == 0)
		out_print("inspect: no samples to display");
}

/*
 * cmd_check_pattern - verify the test-slave incrementing counter pattern.
 *
 * The test slave device produces the following global sample sequence:
 *
 *   [0xAAAA, 0x5555,]  0x0000, 0x0001, 0x0002, ...
 *
 * The counter wraps at the bit width given by the "bits=N" argument
 * (default 16).  For a 12-bit ADC use "bits=12" so the counter wraps
 * at 0x0FFF → 0x0000.  The startup markers 0xAAAA/0x5555 are always
 * full 16-bit values and are never masked.
 *
 * The optional 0xAAAA/0x5555 startup pair is present only when the slave
 * was powered-on or reset before streaming started.  Use "nostart" to
 * skip startup detection and seed the expected counter from the first
 * sample instead.
 *
 * On mismatch the expected counter is re-synced to (actual + 1) & mask
 * so that downstream samples can still be checked independently.
 *
 * Usage: check_pattern [nostart] [bits=N]
 *   N defaults to 16; valid range 1–16.
 */
static void cmd_check_pattern(const char *args)
{
	bool skip_startup = false;
	int  bits         = 16;

	/* Parse arguments: accept "nostart" and/or "bits=N" in any order */
	if (args) {
		const char *p = args;

		while (*p) {
			while (*p == ' ') p++;
			if (strncmp(p, "nostart", 7) == 0 &&
			    (p[7] == '\0' || p[7] == ' ')) {
				skip_startup = true;
				p += 7;
			} else if (strncmp(p, "bits=", 5) == 0) {
				char *end;
				long v = strtol(p + 5, &end, 10);

				if (v >= 1 && v <= 16)
					bits = (int)v;
				else
					out_print("check_pattern: bits must be 1–16"
						  " (got %ld), using 16", v);
				p = end;
			} else {
				/* Skip unknown token */
				while (*p && *p != ' ') p++;
			}
		}
	}

	uint16_t mask = (bits == 16) ? 0xFFFF
				     : (uint16_t)((1u << bits) - 1u);

	unsigned w   = atomic_load_explicit(&g_ring->windex, memory_order_relaxed);
	unsigned rd  = atomic_load_explicit(&g_ring->rindex, memory_order_relaxed);
	unsigned avail = w - rd;

	if (avail > NUM_DATA_CHUNKS)
		avail = NUM_DATA_CHUNKS;

	if (avail == 0) {
		out_print("check_pattern: ring is empty");
		return;
	}

	unsigned oldest = w - avail;

	/* Startup-detection state */
	bool expect_55      = false;	/* saw 0xAAAA; next must be 0x5555 */
	bool startup_found  = false;
	unsigned start_ci   = 0, start_si = 0;

	/* Counter state */
	bool     have_expected = skip_startup;
	uint16_t expected      = 0;

	/* Metrics */
	uint32_t bad_count   = 0;
	uint32_t event_count = 0;	/* transitions good→bad */
	bool     prev_bad    = false;

#define MAX_ERRORS_SHOWN 64
	unsigned errors_shown = 0;
	unsigned long total   = (unsigned long)avail * MAX_SAMPS;

	out_print("--- Pattern check (%u chunks, %lu samples, %d-bit counter) ---",
		  avail, total, bits);

	for (unsigned ci = 0; ci < avail; ci++) {
		const struct adc_data *chunk = ring_chunk_at(oldest, ci);

		for (int si = 0; si < MAX_SAMPS; si++) {
			uint32_t raw = chunk->samples[si];
			uint16_t val = extract_test_val(raw);

			/* --- Phase 1: determine counter seed --- */
			if (!have_expected && !expect_55) {
				/* Startup markers are always full 16-bit — no mask */
				if (!skip_startup && val == 0xAAAA) {
					expect_55 = true;
					start_ci  = ci;
					start_si  = si;
				} else {
					/* No startup marker; seed from first sample */
					expected      = (uint16_t)((val + 1) & mask);
					have_expected = true;
				}
				prev_bad = false;
				continue;
			}

			if (expect_55) {
				expect_55 = false;
				/* 0x5555 is always full 16-bit — no mask */
				if (val == 0x5555) {
					startup_found = true;
					expected      = 0x0000;
					have_expected = true;
					prev_bad      = false;
					continue;
				}
				/* Missing 0x5555 after 0xAAAA — report and resync */
				if (errors_shown < MAX_ERRORS_SHOWN) {
					out_print("  chunk %4u sample %3d:"
						  " startup: expected 0x5555 after 0xAAAA,"
						  " got 0x%04X  raw 0x%08X",
						  ci, si, val, raw);
					errors_shown++;
				}
				bad_count++;
				if (!prev_bad)
					event_count++;
				prev_bad      = true;
				expected      = (uint16_t)((val + 1) & mask);
				have_expected = true;
				continue;
			}

			/* --- Phase 2: counter verification (masked) --- */
			uint16_t val_masked = val & mask;

			if (val_masked != expected) {
				if (!prev_bad)
					event_count++;
				if (errors_shown < MAX_ERRORS_SHOWN) {
					out_print("  chunk %4u sample %3d:"
						  " expected 0x%04X  got 0x%04X"
						  "  raw 0x%08X",
						  ci, si, expected, val_masked, raw);
					errors_shown++;
				}
				bad_count++;
				expected = (uint16_t)((val_masked + 1) & mask);
				prev_bad = true;
			} else {
				expected = (uint16_t)((expected + 1) & mask);
				prev_bad = false;
			}
		}
	}

	if (errors_shown >= MAX_ERRORS_SHOWN)
		out_print("  ... (truncated — first %u errors shown)", MAX_ERRORS_SHOWN);

	out_print("--- Summary ---");
	if (!skip_startup) {
		if (startup_found)
			out_print("  Startup 0xAAAA/0x5555 : found at chunk %u sample %u",
				  start_ci, start_si);
		else
			out_print("  Startup 0xAAAA/0x5555 : not found"
				  " (counter seeded from first sample)");
	}
	out_print("  Counter bits   : %d  (wraps at 0x%04X)", bits, mask);
	out_print("  Total samples  : %lu", total);
	out_print("  Bad  samples   : %u  (%.3f%%)",
		  bad_count,
		  total > 0 ? 100.0 * bad_count / (double)total : 0.0);
	out_print("  Good samples   : %lu  (%.3f%%)",
		  total - bad_count,
		  total > 0 ? 100.0 * (total - bad_count) / (double)total : 0.0);
	out_print("  Error events   : %u  (distinct runs of bad samples)",
		  event_count);
}

static void cmd_help(const char *args)
{
	(void)args;

	out_print("Available commands:");
	for (int i = 0; g_commands[i].name; i++)
		out_print("  %-16s  %s", g_commands[i].name, g_commands[i].help);
	out_print("Scroll output:  PgUp / PgDn  or  mouse wheel");
	out_print("Command history: Up / Down arrow keys");
}

static void cmd_quit(const char *args)
{
	(void)args;
	g_running = false;
}

/* ------------------------------------------------------------------ */
/* Command dispatcher                                                   */
/* ------------------------------------------------------------------ */

static void dispatch(const char *line)
{
	/* Skip leading whitespace */
	while (*line == ' ' || *line == '\t')
		line++;

	if (!*line)
		return;

	/* Split into verb and tail arguments */
	char verb[CMD_MAX_LEN];
	const char *args = "";
	const char *sp = strchr(line, ' ');

	if (sp) {
		size_t len = (size_t)(sp - line);

		if (len >= CMD_MAX_LEN)
			len = CMD_MAX_LEN - 1;
		memcpy(verb, line, len);
		verb[len] = '\0';
		args = sp + 1;
		while (*args == ' ')
			args++;
	} else {
		strncpy(verb, line, CMD_MAX_LEN - 1);
		verb[CMD_MAX_LEN - 1] = '\0';
	}

	out_print("> %s", line);

	for (int i = 0; g_commands[i].name; i++) {
		if (strcmp(verb, g_commands[i].name) == 0) {
			g_commands[i].handler(args);
			return;
		}
	}

	out_print("Unknown command '%s'. Type 'help' for list.", verb);
}

/* ------------------------------------------------------------------ */
/* ncurses layout                                                       */
/* ------------------------------------------------------------------ */

static void setup_windows(void)
{
	int rows, cols;

	getmaxyx(stdscr, rows, cols);

	int out_rows = rows - 1 - INPUT_ROWS;	/* separator + input rows */

	if (out_rows < 3)
		out_rows = 3;

	if (g_out_win) delwin(g_out_win);
	if (g_sep_win) delwin(g_sep_win);
	if (g_inp_win) delwin(g_inp_win);

	g_out_win = newwin(out_rows,	cols, 0,		0);
	g_sep_win = newwin(1,		cols, out_rows,		0);
	g_inp_win = newwin(INPUT_ROWS,	cols, out_rows + 1,	0);

	scrollok(g_out_win, FALSE);
	keypad(g_inp_win, TRUE);
}

static void inp_redraw(const char *buf, int cursor)
{
	wclear(g_inp_win);
	mvwprintw(g_inp_win, 0, 0, "> %s", buf);
	wmove(g_inp_win, 0, 2 + cursor);
	wrefresh(g_inp_win);
}

/* ------------------------------------------------------------------ */
/* Main event / input loop                                              */
/* ------------------------------------------------------------------ */

static void run_ui(void)
{
	char cmd_buf[CMD_MAX_LEN] = { 0 };
	int  cmd_len = 0;
	int  cursor  = 0;

	char history[CMD_HISTORY_SZ][CMD_MAX_LEN];
	int  hist_count = 0;
	int  hist_pos   = -1;	/* -1 = editing fresh line */

	/* Show banner on startup */
	cmd_info(NULL);
	out_print("Type 'help' for available commands, 'quit' to exit.");

	out_refresh();
	sep_refresh();
	inp_redraw(cmd_buf, cursor);

	while (g_running) {
		int ch = wgetch(g_inp_win);

		switch (ch) {

		case KEY_RESIZE:
			setup_windows();
			out_refresh();
			sep_refresh();
			inp_redraw(cmd_buf, cursor);
			break;

		/* --- Output scrolling --- */
		case KEY_PPAGE:		/* Page Up   */
			g_out_scroll += getmaxy(g_out_win);
			out_refresh();
			sep_refresh();
			break;

		case KEY_NPAGE:		/* Page Down */
			g_out_scroll -= getmaxy(g_out_win);
			if (g_out_scroll < 0)
				g_out_scroll = 0;
			out_refresh();
			sep_refresh();
			break;

		/* --- Mouse wheel scrolling --- */
		case KEY_MOUSE: {
			MEVENT ev;

			if (getmouse(&ev) == OK) {
				if (ev.bstate & BUTTON4_PRESSED) {
					/* Scroll up */
					g_out_scroll += MOUSE_SCROLL_LINES;
					out_refresh();
					sep_refresh();
				} else if (ev.bstate & BUTTON5_PRESSED) {
					/* Scroll down */
					g_out_scroll -= MOUSE_SCROLL_LINES;
					if (g_out_scroll < 0)
						g_out_scroll = 0;
					out_refresh();
					sep_refresh();
				}
			}
			break;
		}

		/* --- Command history --- */
		case KEY_UP:
			if (hist_count > 0 && hist_pos < hist_count - 1) {
				hist_pos++;
				int hi = (hist_count - 1 - hist_pos) % CMD_HISTORY_SZ;

				strncpy(cmd_buf, history[hi], CMD_MAX_LEN - 1);
				cmd_buf[CMD_MAX_LEN - 1] = '\0';
				cmd_len = strlen(cmd_buf);
				cursor  = cmd_len;
				inp_redraw(cmd_buf, cursor);
			}
			break;

		case KEY_DOWN:
			if (hist_pos > 0) {
				hist_pos--;
				int hi = (hist_count - 1 - hist_pos) % CMD_HISTORY_SZ;

				strncpy(cmd_buf, history[hi], CMD_MAX_LEN - 1);
				cmd_buf[CMD_MAX_LEN - 1] = '\0';
				cmd_len = strlen(cmd_buf);
				cursor  = cmd_len;
			} else {
				hist_pos    = -1;
				cmd_buf[0]  = '\0';
				cmd_len     = 0;
				cursor      = 0;
			}
			inp_redraw(cmd_buf, cursor);
			break;

		/* --- Cursor movement --- */
		case KEY_LEFT:
			if (cursor > 0) {
				cursor--;
				inp_redraw(cmd_buf, cursor);
			}
			break;

		case KEY_RIGHT:
			if (cursor < cmd_len) {
				cursor++;
				inp_redraw(cmd_buf, cursor);
			}
			break;

		case KEY_HOME:
		case 1:		/* Ctrl-A */
			cursor = 0;
			inp_redraw(cmd_buf, cursor);
			break;

		case KEY_END:
		case 5:		/* Ctrl-E */
			cursor = cmd_len;
			inp_redraw(cmd_buf, cursor);
			break;

		/* --- Editing --- */
		case KEY_BACKSPACE:
		case 127:
		case '\b':
			if (cursor > 0) {
				memmove(&cmd_buf[cursor - 1], &cmd_buf[cursor],
					cmd_len - cursor + 1);
				cmd_len--;
				cursor--;
				inp_redraw(cmd_buf, cursor);
			}
			break;

		case KEY_DC:	/* Delete key */
			if (cursor < cmd_len) {
				memmove(&cmd_buf[cursor], &cmd_buf[cursor + 1],
					cmd_len - cursor);
				cmd_len--;
				inp_redraw(cmd_buf, cursor);
			}
			break;

		case 21:	/* Ctrl-U: clear line */
			cmd_buf[0] = '\0';
			cmd_len    = 0;
			cursor     = 0;
			inp_redraw(cmd_buf, cursor);
			break;

		case 11:	/* Ctrl-K: clear to end of line */
			cmd_buf[cursor] = '\0';
			cmd_len = cursor;
			inp_redraw(cmd_buf, cursor);
			break;

		/* --- Execute --- */
		case '\n':
		case '\r':
		case KEY_ENTER:
			if (cmd_len > 0) {
				/* save to history */
				strncpy(history[hist_count % CMD_HISTORY_SZ],
					cmd_buf, CMD_MAX_LEN - 1);
				hist_count++;
				hist_pos = -1;

				dispatch(cmd_buf);

				cmd_buf[0] = '\0';
				cmd_len    = 0;
				cursor     = 0;

				/* Jump back to latest output */
				g_out_scroll = 0;
				out_refresh();
				sep_refresh();
			}
			inp_redraw(cmd_buf, cursor);
			break;

		default:
			/* Printable ASCII */
			if (ch >= 32 && ch < 127 && cmd_len < CMD_MAX_LEN - 1) {
				memmove(&cmd_buf[cursor + 1], &cmd_buf[cursor],
					cmd_len - cursor + 1);
				cmd_buf[cursor] = (char)ch;
				cmd_len++;
				cursor++;
				inp_redraw(cmd_buf, cursor);
			}
			break;
		}
	}
}

/* ------------------------------------------------------------------ */
/* File loading                                                         */
/* ------------------------------------------------------------------ */

static struct mvaring *load_ring_file(const char *path)
{
	int fd = open(path, O_RDONLY);

	if (fd < 0) {
		perror(path);
		return NULL;
	}

	struct stat st;

	if (fstat(fd, &st) < 0) {
		perror("fstat");
		close(fd);
		return NULL;
	}

	if ((size_t)st.st_size < sizeof(struct mvaring)) {
		fprintf(stderr,
			"%s: file too small (%zu bytes, need at least %zu)\n",
			path, (size_t)st.st_size, sizeof(struct mvaring));
		close(fd);
		return NULL;
	}

	void *addr = mmap(NULL, (size_t)st.st_size, PROT_READ,
			  MAP_PRIVATE, fd, 0);
	close(fd);

	if (addr == MAP_FAILED) {
		perror("mmap");
		return NULL;
	}

	g_file_size = (size_t)st.st_size;
	return (struct mvaring *)addr;
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <ring-buffer-file>\n", argv[0]);
		fprintf(stderr,
			"  Opens a binary mvaring snapshot and provides an "
			"interactive analysis shell.\n");
		return 1;
	}

	g_filename = argv[1];
	g_ring = load_ring_file(g_filename);
	if (!g_ring)
		return 1;

	if (!ring_is_ok(g_ring)) {
		fprintf(stderr,
			"%s: not a valid mvaring ring buffer\n", g_filename);
		fprintf(stderr, "  version: %u (expected %u)\n",
			g_ring->version, MVARING_VERSION);
		fprintf(stderr, "  size:    %u (expected >= %zu)\n",
			g_ring->size, sizeof(struct mvaring));
		munmap(g_ring, g_file_size);
		return 1;
	}

	/* Initialise ncurses */
	initscr();
	cbreak();
	noecho();
	curs_set(1);

	if (has_colors()) {
		start_color();
		use_default_colors();
	}

	/* Enable mouse wheel events (BUTTON4 = scroll up, BUTTON5 = scroll down) */
	mousemask(BUTTON4_PRESSED | BUTTON5_PRESSED, NULL);

	setup_windows();
	run_ui();

	endwin();
	munmap(g_ring, g_file_size);
	printf("Bye.\n");
	return 0;
}
