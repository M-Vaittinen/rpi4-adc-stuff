#ifndef MVABUF_H
#define MVABUF_H

#ifdef __cpp
extern "C" {
#endif

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common.h"

#define MVARING_VERSION 1

struct adc_data {
	uint32_t usecs;
	uint32_t samples[MAX_SAMPS];
};

struct mvaring {
	uint8_t version;  /* ring buffer version */
	uint8_t writing;  /* sequence-lock, might be unnecessary */
	uint16_t dropped; /* counter for overwritten entries (too slow reader) */
	uint32_t size;    /* Size of the ring (should equal sizeof(struct mvaring)) */
	atomic_uint rindex;
	atomic_uint windex;
	struct adc_data buf[NUM_DATA_CHUNKS];
};

struct mvaring * ring_init(void *buff, size_t bufsize);
bool ring_is_ok(struct mvaring *r);
/*
 * Ring add returns zero when a value was added to a buffer, and buffer had
 * space. Non zero value is returned if there was not enough space in the ring
 * (whether or not the old values were overwritten).
 *
 * The @dropfull controls whether the data is dropped when ring is full, or if
 * the old data is overwritten. Setting dropfull to true will cause new data
 * to be dropped, setting it false makes old to be overwritten.
 */
int ring_add(struct mvaring *r, const struct adc_data *data, bool dropfull);
int ring_read(struct mvaring *r, struct adc_data *buf, unsigned int num_chunks);

#ifdef __cpp
}
#endif

#endif
