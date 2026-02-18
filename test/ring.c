#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>  /* sched_yield() , set_affinity()*/
#include <stdio.h>
#include <stdlib.h> /* exit */
#include <string.h> /* memcmp */
#include <unistd.h> /* fork, usleep */

#include "mva_test.h"
#include "../rpi_shmem.h"
#include "../mvaring.h"

/* copy 1M sets of samples */
#define NUM_TEST_ENTRIES 1000000

//static char g_buff;
static struct shmem_info g_i;

static unsigned int g_rx;
static unsigned int g_tx;

static struct adc_data g_rxdata[5];
static struct adc_data g_txdata = {
	.samples = { 1,2,3,4,5,6,7,8,9,10 },
};
static const uint32_t g_samplecmp[] = { 1,2,3,4,5,6,7,8,9,10 };

static int test_ring_init(struct mvaring **mr)
{
	struct mvaring *rng;

	rng = ring_init(g_i.buff, sizeof(*rng) - 1);
	MVA_CHECK(rng, -EINVAL, "ring init succeeded with too small buffer");

	rng = ring_init(g_i.buff, g_i.size);
	MVA_CHECK(!rng, -ENOMEM, "ring init failed");

	*mr = rng;

	return 0;
}

static int buffer_prepare()
{
	return shmem_create("/mvaringtest", sizeof(struct mvaring), &g_i);
}

static void dbg_adcdata(struct adc_data *d, unsigned int ctr)
{
	int i;

	printf("Data: %u usecs (exp %u), samples:", d->usecs, ctr);
	for (i = 0; i < 10; i++)
		printf(" %d", d->samples[i]);
	printf("\n");
}

//static struct adc_data g_dbg;

static int test_write_item(struct mvaring *mr)
{
	int i;
//	static int foo = 0;

	for (i = 0; i < 7 && g_tx < NUM_TEST_ENTRIES; i++) {
		g_txdata.usecs = g_tx ++;
/*		if (!foo) {
			printf("sending\n");
			dbg_adcdata(&g_txdata, i);
		}*/
		ring_add(mr, &g_txdata);
/*		{
			if (!foo) {
				int ret;
				
				ret = ring_read(mr, &g_dbg, 1);
				if (ret == 1) {
					printf("selfrecv'd\n");
					dbg_adcdata(&g_dbg, i);
				}

			}
		}*/
	}
	//foo = 1;
	/*
	 * This is not really TheRightThingToDo(tm) - unless we use FIFO/RR
	 * schedulers. That's something we may want to consider though.
	 */
	sched_yield();
//	SPINAWHILE();
//	usleep(0);

	return 0;
}

bool rx_ok(struct adc_data *rxdata)
{
	static unsigned ctr;
	bool ok = false;

	if (rxdata && rxdata->usecs == ctr &&
	    !memcmp(&rxdata->samples[0], &g_samplecmp[0], sizeof(g_samplecmp)))
		ok = true;


	if (!ok) {
		printf("Recv'd bad data:\n");
		dbg_adcdata(rxdata, ctr);
	}

	ctr++;

	return ok;
}

static int test_read_and_verify(struct mvaring *mr)
{
	int ret, i;
	unsigned int ctr = 0;

	do {
		ret = ring_read(mr, &g_rxdata[0], 5);
		ctr ++;
	} while (ret == -EAGAIN && ctr < 0xfffffff0);

	if (ret < 0) {
		if (ret == -EAGAIN)
			printf("No data received\n");
		printf("ring_read FAILED\n");

		return ret;
	}
	for (i = 0; i < ret; i++) {
		MVA_CHECK(!rx_ok(&g_rxdata[i]), -EINVAL, "Bad data recv'd\n");
		g_rx++;
	}

	return 0;
}

#define TEST_SCHED_PRIO 10

int set_sched()
{
	struct sched_param param;
	int policy = SCHED_FIFO;
	int ret;

	param.sched_priority = TEST_SCHED_PRIO;
	if (sched_setscheduler(0, policy, &param) == -1) {
		ret = errno;
		printf("sched_setscheduler failed: %s\n", strerror(errno));

		return ret;
	}

	return 0;
}

int main()
{
	struct mvaring *mr;
	pid_t pid;
	int ret;
	cpu_set_t set;

	/*
	 * Init the SHM and ring struct before fork() to avoid the need for
	 * synchronization between processes.
	 */
	ret = buffer_prepare();
	if (ret)
		return ret;

	ret = test_ring_init(&mr);
	if (ret) {
		printf("ring_init test FAILED\n");
		return ret;
	}

	set_sched();

	/*
	 * I don't really remember whether the shared-memory addresses stay
	 * valid both on parent and child at fork(). Lets see. If there is
	 * a problem, we can just re-open the shm and map the ring pointer
	 * to newly opened memory. The ring struct should be initialized
	 * there.
	 *
	 * TODO: Add also a ring_open() function, which takes the (shm)
	 * buffer pointer to a ring that has already been initialized
	 * (possibly by another process) and checks at least the sanity of
	 * the version field.
	 */
	pid = fork();
	if (pid < 0) {
		perror("Fork failed");
		ret = pid;
		goto clean_out;
	}

	if (!ring_is_ok(mr)) {
		printf("Bad ring - test FAILED\n");
		return -EINVAL;
	}

	if (pid == 0) {

		CPU_ZERO(&set);
		CPU_SET(0, &set);
		ret = sched_setaffinity(0, sizeof(cpu_set_t), &set);
		if (ret)
			perror("set affinity");

		do {
			ret = test_read_and_verify(mr);
		} while (!ret && g_rx < NUM_TEST_ENTRIES);

		if (ret)
			exit(ret);
		exit(0);
	}

	CPU_ZERO(&set);        // clear cpu mask
	CPU_SET(0, &set);      // set cpu 0
	ret = sched_setaffinity(0, sizeof(cpu_set_t), &set);
	if (ret)
		perror("set affinity");

	usleep(10000);

	do {
		ret = test_write_item(mr);
	} while (!ret && g_tx < NUM_TEST_ENTRIES);

	printf("test done: %u dropped, rindex %u, windex %u\n", mr->dropped, mr->rindex, mr->windex);

clean_out:
	if (g_i.buff)
		shmem_destroy(&g_i);

	if (ret)
		printf("FAILED\n");
	else
		printf("PASSED\n");

	return ret;
}
