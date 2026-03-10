#include <errno.h>
#include <stdio.h>

#include "adc_common.h"
#include "rpi_helpers.h"

int rpi_shm_create(struct shmem_info *shi, struct mvaring **mr)
{
	int ret;

	ret = shmem_create(SHM_NAME, SHM_SIZE, shi);
	if (ret) {
		printf("shmem_create failed. Name %s, size %lu\n", SHM_NAME, (unsigned long)SHM_SIZE);

		return ret;
	}

	*mr = ring_init(shi->buff, SHM_SIZE);
	if (!*mr) {
		printf("Ringbuffer init failed\n");
		return -EINVAL;
	}

	return 0;
}


