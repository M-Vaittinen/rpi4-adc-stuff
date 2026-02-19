#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "adc_common.h"
#include "common.h"
#include "rpi_shmem.h"
#include "mvaring.h"

static struct adc_data data[10];
/* first 2 chuncks of data to guesstimate clk */
static struct adc_data start_data[2];

void store_one(FILE *wf, struct adc_data *a, uint32_t nsec_delta)
{
	uint64_t time = a->usecs * 1000;
	int i;

	for (i = 0; i < MAX_SAMPS; i++)
		fprintf(wf, "%llu\t%u\n",time + i * nsec_delta, ((a->samples[i] << 8) | (a->samples[i] >> 8)) & 0xffff);

}

void store_adc(FILE *wf, struct adc_data *a, int num_a, uint32_t nsec_delta)
{
	int i;

	for (i = 0; i < num_a; i++)
		store_one(wf, &a[i], nsec_delta);
}

int main(int argc, const char *argv[])
{
	struct shmem_info in;
	struct mvaring *mr;
	uint32_t nsec_delta;
	int ret;

	FILE *wf;
	
	wf = fopen("data_out", "w");
	if (!wf) {
		ret = errno;
		perror("fopen");
		return ret;
	}

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
	}

	if (0) {
err_out:
		printf("FAIL! %d\n", ret);
	}
	fclose(wf);
	shmem_close(&in);

	return ret;
}
