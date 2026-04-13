#include <errno.h>
#include <stdio.h>
#include <unistd.h>

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

/*
static void shmem_close_and_reset(struct shmem_info *shi)
{
       if (!shi)
               return;

       if (shi->buff)
               shmem_close(shi);

       shi->buff = NULL;
       shi->size = 0;
       shi->fd = -1;
}
*/

int open_existing_shm_wait(struct shmem_info *shi, struct mvaring **mr)
{
       int ret;

       for (;;) {
               ret = shmem_open(SHM_NAME, SHM_SIZE, shi, true);
               if (!ret)
                       break;

               if (ret != -ENOENT)
                       fprintf(stderr, "Waiting for SHM open (%d)\n", ret);

               usleep(100000);
       }

       *mr = shi->buff;
       while (!ring_is_ok(*mr))
               usleep(10000);

       return 0;
}

