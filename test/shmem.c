#include <stdio.h>
#include "../rpi_shmem.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(_arr) (sizeof(_arr)/sizeof(_arr[0]))
#endif

#define MVA_CHECK(cond, _ret, reason, ...) if (cond) { printf((reason), ## __VA_ARGS__); return (_ret); } else {}

static struct shmem_info g_i;

void clean_up()
{
	if (g_i.buff)
		shmem_destroy(&g_i);
}

int create_test()
{
	int ret, i;
	const char *name = "/shmemtest";
	const size_t size = 1024;
	const char *bad_name[] = { "foo", "", "/"};
	size_t bad_size = 0;

	/* Try create with bad name */
	for (i = 0; i < ARRAY_SIZE(bad_name); i++) {
		ret = shmem_create(bad_name[i], size, &g_i);
		MVA_CHECK(!ret, -1, "bad name '%s' returned success\n", bad_name[i]);
	}
	printf("Testing bad names PASSED\n");

	/* Invalid size: */	
	ret = shmem_create(name, bad_size, &g_i);
	MVA_CHECK(!ret, -1,"bad size '%lu' returned success\n", bad_size);

	printf("Testing bad size PASSED\n");

	printf("Testing successfull create...\n");
	ret = shmem_create(name, size, &g_i);
	MVA_CHECK(ret, ret, "Unexpected ret %d\n", ret);
	MVA_CHECK(!g_i.buff, -1, "NULL buff");
       	MVA_CHECK(g_i.size != size, -1, "unexpected size %lu\n", g_i.size);
	printf("\t ...PASSED\n");

	clean_up();

	return 0;
}


int main()
{
	int ret;

	ret = create_test();
	if (ret) {
		printf("Create test FAILED\n");
		return ret;
	}

	return 0;
}
