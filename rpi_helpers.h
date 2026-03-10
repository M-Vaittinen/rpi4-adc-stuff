#ifndef _RPI_HELPERS_H
#define _RPI_HELPERS_H

#include "mvaring.h"
#include "rpi_shmem.h"

int rpi_shm_create(struct shmem_info *shi, struct mvaring **mr);

#endif
