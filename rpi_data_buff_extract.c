#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "adc_common.h"
#include "common.h"
#include "rpi_shmem.h"
#include "mvaring.h"

/* TODO: Use real bitmask (12 bits?) */
#define ADC_BITMASK 0xffff

static struct adc_data data[10];
/* first 2 chuncks of data to guesstimate clk */
static struct adc_data start_data[2];
static bool g_output_gpio = true;  /* Output GPIO data by default */

#define RAW2SAMP(raw) (((uint16_t)(raw) >> 8 | (uint16_t)raw << 8) & ADC_BITMASK)

void store_one(FILE *wf, struct adc_data *a, uint32_t nsec_delta)
{
	uint64_t time = a->usecs * 1000;
	int i;

	if (g_output_gpio) {
		for (i = 0; i < MAX_SAMPS; i++)
			fprintf(wf, "%llu\t%u\t0x%08x\n", time + i * nsec_delta, 
				RAW2SAMP(a->samples[i]), a->gpio_lev0[i]);
	} else {
		for (i = 0; i < MAX_SAMPS; i++)
			fprintf(wf, "%llu\t%u\n", time + i * nsec_delta, 
				RAW2SAMP(a->samples[i]));
	}
}

void store_adc(FILE *wf, struct adc_data *a, int num_a, uint32_t nsec_delta)
{
	int i;

	for (i = 0; i < num_a; i++)
		store_one(wf, &a[i], nsec_delta);
}

static void print_usage(const char *prog_name)
{
	printf("Usage: %s [options]\n", prog_name);
	printf("Extract ADC data from shared memory ring buffer\n\n");
	printf("Options:\n");
	printf("  -g, --no-gpio    Disable GPIO output (ADC only)\n");
	printf("  -h, --help       Show this help message\n\n");
	printf("Output format:\n");
	printf("  With GPIO (default): timestamp(ns)  adc_value  gpio_state(hex)\n");
	printf("  Without GPIO:        timestamp(ns)  adc_value\n");
	printf("\nOutput file: out/data_out\n");
}

int main(int argc, char *argv[])
{
	struct shmem_info in;
	struct mvaring *mr;
	uint32_t nsec_delta;
	int ret;
	int opt;

	FILE *wf;

	static struct option long_options[] = {
		{"no-gpio", no_argument, NULL, 'g'},
		{"help",    no_argument, NULL, 'h'},
		{NULL,      0,           NULL, 0}
	};

	/* Parse command line arguments */
	while ((opt = getopt_long(argc, argv, "gh", long_options, NULL)) != -1) {
		switch (opt) {
		case 'g':
			g_output_gpio = false;
			break;
		case 'h':
			print_usage(argv[0]);
			return 0;
		default:
			fprintf(stderr, "Use -h for help\n");
			return 1;
		}
	}
	
	wf = fopen("out/data_out", "w");
	if (!wf) {
		ret = errno;
		perror("fopen");
		return ret;
	}

	/* Write header comment */
	if (g_output_gpio)
		fprintf(wf, "# timestamp(ns)\tadc_value\tgpio_lev0(hex)\n");
	else
		fprintf(wf, "# timestamp(ns)\tadc_value\n");

	ret = shmem_open(SHM_NAME, SHM_SIZE, &in);
	if (ret) {
		printf("Nooo\n");
		return ret;
	}

	mr = in.buff;

	while (!ring_is_ok(mr))
		sleep(0);

	for (ret = 0; ret >= 0 && ret < 2;) {
		ret = ring_read(mr, &start_data[ret], 2 - ret);
		if (ret == -EAGAIN)
			ret = 0;
	}

	if (ret < 0)
		goto err_out;

	nsec_delta = (start_data[1].usecs - start_data[0].usecs) * 1000;
	nsec_delta /= MAX_SAMPS;

	store_adc(wf, &start_data[0], 2, nsec_delta);

	for (;;) {
		ret = ring_read(mr, &data[0], ARRAY_SIZE(data));
		if (!ret || ret == -EAGAIN)
			continue;

		if (ret < 0)
			goto err_out;

		store_adc(wf, &data[0], ret, nsec_delta);

		if (ring_empty(mr))
			break;
	}

	if (0) {
err_out:
		printf("FAIL! %d\n", ret);
	}
	fclose(wf);
	shmem_close(&in);

	return ret;
}
