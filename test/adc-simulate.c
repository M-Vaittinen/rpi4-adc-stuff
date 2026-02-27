/*
 * ADC data simulator for testing UI trigger functionality
 * Generates sine wave data and writes to shared memory ring buffer
 * Supports interactive trigger control via command interface
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>

#include "../adc_common.h"
#include "../mvaring.h"
#include "../rpi_shmem.h"

#define M_PI 3.14159265358979323846

/* Simulation parameters */
#define SAMPLE_RATE 1000000  /* 1 MSPS */
#define FREQ_HZ 1000         /* 1 kHz sine wave */
#define ADC_MAX_VALUE 4096   /* 12-bit ADC */
#define V_MIN 0.0
#define V_MAX 3.3

/* GPIO simulation */
static volatile uint32_t g_gpio_state = 0;  /* Current GPIO state (all 32 bits) */
static volatile int g_running = 1;

/* Ring buffer */
static struct mvaring *g_ring = NULL;
static struct shmem_info g_shm;

/* Convert voltage to ADC value */
static uint32_t voltage_to_adc(float voltage)
{
	if (voltage < V_MIN)
		voltage = V_MIN;
	if (voltage > V_MAX)
		voltage = V_MAX;
	
	return (uint32_t)((voltage / V_MAX) * ADC_MAX_VALUE);
}

/* Generate sine wave sample */
static float sine_sample(uint64_t sample_num)
{
	double t = (double)sample_num / SAMPLE_RATE;
	double sine = sin(2.0 * M_PI * FREQ_HZ * t);
	
	/* Map sine [-1, 1] to voltage [V_MIN, V_MAX] */
	return (sine + 1.0) / 2.0 * (V_MAX - V_MIN) + V_MIN;
}

/* Signal handler for clean shutdown */
static void signal_handler(int sig)
{
	(void)sig;
	printf("\nShutting down...\n");
	g_running = 0;
}

/* Data generator thread */
static void *data_generator(void *arg)
{
	uint64_t sample_count = 0;
	struct adc_data chunk;
	struct timespec start_time, current_time;
	uint64_t elapsed_ns, expected_ns;
	int i;

	(void)arg;

	clock_gettime(CLOCK_MONOTONIC, &start_time);

	printf("Data generator started - writing at 1 MSPS\n");

	while (g_running) {
		/* Generate one chunk of samples */
		chunk.usecs = (uint32_t)((sample_count * 1000000ULL) / SAMPLE_RATE);

		for (i = 0; i < MAX_SAMPS; i++) {
			float voltage = sine_sample(sample_count);
			chunk.samples[i] = voltage_to_adc(voltage);
			chunk.gpio_lev0[i] = g_gpio_state;  /* Current GPIO state */
			sample_count++;
		}

		/* Write to ring buffer */
		if (ring_add(g_ring, &chunk, false) != 0) {
			/* Ring full, data overwritten - this is normal */
		}

		/* Calculate how long we should have taken */
		expected_ns = (sample_count * 1000000000ULL) / SAMPLE_RATE;
		
		/* Check current time */
		clock_gettime(CLOCK_MONOTONIC, &current_time);
		elapsed_ns = (current_time.tv_sec - start_time.tv_sec) * 1000000000ULL +
		             (current_time.tv_nsec - start_time.tv_nsec);

		/* Sleep if we're ahead of schedule */
		if (expected_ns > elapsed_ns) {
			struct timespec sleep_time;
			uint64_t sleep_ns = expected_ns - elapsed_ns;
			
			sleep_time.tv_sec = sleep_ns / 1000000000ULL;
			sleep_time.tv_nsec = sleep_ns % 1000000000ULL;
			nanosleep(&sleep_time, NULL);
		}
	}

	printf("Data generator stopped. Generated %llu samples\n",
	       (unsigned long long)sample_count);
	return NULL;
}

/* Print help */
static void print_help(void)
{
	printf("\nCommands:\n");
	printf("  trigger <gpio> <value>  - Set GPIO pin to value (0 or 1)\n");
	printf("                            Example: trigger 25 1\n");
	printf("  show                    - Show current GPIO state\n");
	printf("  stats                   - Show ring buffer statistics\n");
	printf("  help                    - Show this help\n");
	printf("  quit                    - Exit simulator\n");
	printf("\n");
}

/* Command interface */
static void command_interface(void)
{
	char line[256];
	char cmd[64];
	int gpio, value;

	print_help();

	while (g_running) {
		printf("sim> ");
		fflush(stdout);

		if (!fgets(line, sizeof(line), stdin))
			break;

		/* Parse command */
		if (sscanf(line, "%63s", cmd) != 1)
			continue;

		if (strcmp(cmd, "trigger") == 0) {
			if (sscanf(line, "%*s %d %d", &gpio, &value) == 2) {
				if (gpio < 0 || gpio > 31) {
					printf("Error: GPIO must be 0-31\n");
					continue;
				}
				if (value != 0 && value != 1) {
					printf("Error: Value must be 0 or 1\n");
					continue;
				}

				/* Toggle GPIO bit */
				if (value)
					g_gpio_state |= (1U << gpio);
				else
					g_gpio_state &= ~(1U << gpio);

				printf("GPIO%d set to %d (state=0x%08x)\n",
				       gpio, value, g_gpio_state);
			} else {
				printf("Usage: trigger <gpio> <value>\n");
			}
		} else if (strcmp(cmd, "show") == 0) {
			int i;
			
			printf("GPIO state: 0x%08x\n", g_gpio_state);
			printf("Active GPIOs: ");
			for (i = 0; i < 32; i++) {
				if (g_gpio_state & (1U << i))
					printf("%d ", i);
			}
			printf("\n");
		} else if (strcmp(cmd, "stats") == 0) {
			printf("Ring buffer stats:\n");
			printf("  Version: %d\n", g_ring->version);
			printf("  Dropped: %u\n", g_ring->dropped);
			printf("  Available: %u chunks\n", ring_available(g_ring));
			printf("  Space: %u chunks\n", ring_space(g_ring));
			printf("  Full: %s\n", ring_full(g_ring) ? "yes" : "no");
		} else if (strcmp(cmd, "help") == 0) {
			print_help();
		} else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
			g_running = 0;
		} else {
			printf("Unknown command: %s (type 'help' for commands)\n", cmd);
		}
	}
}

int main(void)
{
	pthread_t gen_thread;
	int ret;

	printf("ADC Simulator - Testing UI trigger functionality\n");
	printf("Generating %d Hz sine wave, 0-%.1f V, at %d samples/sec\n\n",
	       FREQ_HZ, V_MAX, SAMPLE_RATE);

	/* Setup signal handlers */
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	/* Create shared memory ring buffer */
	ret = shmem_create(SHM_NAME, SHM_SIZE, &g_shm);
	if (ret) {
		printf("Failed to create shared memory: %d\n", ret);
		printf("Make sure no other instance is running.\n");
		return 1;
	}

	/* Initialize ring buffer */
	g_ring = ring_init(g_shm.buff, g_shm.size);
	if (!g_ring) {
		printf("Failed to initialize ring buffer\n");
		shmem_destroy(&g_shm);
		return 1;
	}

	printf("Shared memory ring buffer created: %s\n", SHM_NAME);
	printf("Ring buffer version: %d, size: %u bytes\n\n",
	       g_ring->version, g_ring->size);

	/* Start data generator thread */
	ret = pthread_create(&gen_thread, NULL, data_generator, NULL);
	if (ret) {
		printf("Failed to create data generator thread: %d\n", ret);
		shmem_destroy(&g_shm);
		return 1;
	}

	/* Run command interface */
	command_interface();

	/* Wait for generator thread to finish */
	printf("Waiting for data generator to stop...\n");
	pthread_join(gen_thread, NULL);

	/* Cleanup */
	printf("Cleaning up...\n");
	shmem_destroy(&g_shm);

	printf("Simulator stopped.\n");
	return 0;
}
