#include <stdio.h>
#include <stdlib.h> /* exit */
#include <string.h> /* memcmp */
#include <unistd.h> /* Fork */

#include "mva_test.h"
#include "../rpi_shmem.h"

static struct shmem_info g_i;

static void clean_up()
{
	if (g_i.buff)
		shmem_destroy(&g_i);
}

static int mva_create(const char *name, const size_t size)
{
	int ret;

	ret = shmem_create(name, size, &g_i);
	MVA_CHECK(ret, ret, "Unexpected ret %d\n", ret);
	MVA_CHECK(!g_i.buff, -1, "NULL buff");
	MVA_CHECK(g_i.size != size, -1, "unexpected size %lu\n", g_i.size);

	return 0;
}

static int create_test()
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
	ret = mva_create(name, size);
	if (ret)
		return ret;
	printf("\t ...PASSED\n");

	clean_up();

	return 0;
}

static int mva_open(const char *name, const size_t size)
{
	int ret;

	ret = shmem_open(name, size, &g_i);
	MVA_CHECK(ret, -1, "Failed to open shm '%s',size %lu - ret %d\n", name, size, ret);
	MVA_CHECK(!g_i.buff, -2, "NULL buffer after shmem_open()\n");
	MVA_CHECK(size != g_i.size, -2, "Unexpected size %lu, expected %lu\n", g_i.size, size);

	return 0;
}

#define TEST_STRING "Robotti oon, ja tahdon rokata. Voisko joku minut ohjelmoida?"
static char g_arr[] = { 'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z' };

static int child_test_open_read_and_write(const char *name, const size_t size)
{
	int ret;
	size_t compared = 0;
	size_t cmp_chunk_size = sizeof(g_arr);
	char *buff;

	ret = mva_open(name, size);
	if (ret)
		return ret;

	buff = g_i.buff;

	for (compared = 0; cmp_chunk_size < size - compared;
	     compared += cmp_chunk_size) {
		char *cmp_ptr = buff + compared;

		MVA_CHECK(memcmp(cmp_ptr, &g_arr[0], cmp_chunk_size), -1, "Unexpected memory content near %lu\n", compared);
	}

	printf("CHILD: shm contents as expected - proceed writing\n");

	strcpy(buff, TEST_STRING);
	buff[size - 1] = 0;
	shmem_close(&g_i);

	printf("Child: PASSED\n");

	return 0;
}

static int parent_test_open_read(const char *name, const size_t size)
{
	int i, ret;
	char *buff;

	ret = mva_open(name, size);
	if (ret) {
		if (ret != -1)
			shmem_destroy(&g_i); /* This may be a hazard as name/size were unexpected */
		return ret;
	}

	buff = g_i.buff;

	/* Ha. I don't remember anything about shm and caches. Let's see... */
	for (i = 0; buff[size -1] != 0; i++) {
		sleep(1);
		if (i > 10) {
			printf("Child newer grew up?");
			shmem_destroy(&g_i);
			return -1;
		}
	}

	if (strcmp(buff, TEST_STRING))
	       printf("Unexpected data in shm\n");
	else
		printf("SHM test, parent PASSED\n");

	clean_up();

	return 0;
}

static inline char pick_char(size_t index)
{
	return g_arr[index % sizeof(g_arr)];
}

static void fill_mem_pattern(char *buff, const size_t size)
{
	size_t i;

	for (i = 0; i < size; i++)
		buff[i] = pick_char(i);
}

static int use_test()
{
	const char *name = "/shmemtest";
	const size_t size = 1024;
	pid_t pid;
	int ret;

	ret = mva_create(name, size);
	if (ret)
		return ret;

	fill_mem_pattern(g_i.buff, g_i.size);

	shmem_close(&g_i);

	pid = fork();

	if (pid < 0) {
		perror("Fork failed");
		return -1;
	} else if (pid == 0) {
		ret = child_test_open_read_and_write(name, size);
		if (ret)
			exit(ret);
		exit(0);
	}

	return parent_test_open_read(name, size);
}

int main()
{
	int ret;

	ret = create_test();
	if (ret) {
		printf("Create test FAILED\n");
		return ret;
	}

	ret = use_test();
	if (ret)
		printf("Use test failed\n");

	return ret;
}
