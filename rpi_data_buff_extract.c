#include <errno.h>
#include <getopt.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "adc_common.h"
#include "common.h"
#include "rpi_shmem.h"
#include "mvaring.h"

#define ADC_BITMASK 0xffff
#define STREAM_PORT_DEFAULT 9000
#define STREAM_BACKLOG 1
#define STREAM_BUFFLEN (MAX_SAMPS * 6 + 2)

static struct adc_data data[10];
static bool g_keep_buffer;		/* Keep the shared-memory after shutdown */
static bool g_use_old_buff;		/* Use existing SHM */
static bool g_shm_drop;			/* Drop SHM and exit */
static uint16_t g_stream_port = STREAM_PORT_DEFAULT;

static char g_stream_line[STREAM_BUFFLEN];

#define RAW2SAMP(raw) (((uint16_t)(raw) >> 8 | (uint16_t)raw << 8) & ADC_BITMASK)

static int send_all(int fd, const char *buf, size_t len)
{
	while (len) {
		ssize_t sent = send(fd, buf, len, 0);
		if (sent <= 0)
			return -1;
		buf += sent;
		len -= (size_t)sent;
	}

	return 0;
}

static ssize_t format_adc_csv(const struct adc_data *a, char *out, size_t out_len)
{
	static unsigned long long chunk_count;
	size_t off = 0;
	uint16_t first = RAW2SAMP(a->samples[0]);
	uint16_t last = RAW2SAMP(a->samples[MAX_SAMPS - 1]);

	chunk_count++;
	if (chunk_count <= 10 || (chunk_count % 100) == 0) {
		fprintf(stderr,
			"format_adc_csv: chunk=%llu usecs=%u first=%u last=%u\n",
			chunk_count, a->usecs, first, last);
	}

	for (int i = 0; i < MAX_SAMPS; i++) {
		int written = snprintf(out + off, out_len - off, "%u%c",
			       RAW2SAMP(a->samples[i]), (i == MAX_SAMPS - 1) ? '\n' : ',');
		if (written < 0)
			return -1;
		if ((size_t)written >= out_len - off)
			return -1;
		off += (size_t)written;
	}

	return (ssize_t)off;
}

static void print_usage(const char *prog_name)
{
	printf("Usage: %s [options]\n", prog_name);
	printf("Stream ADC data from shared memory ring buffer over TCP\n\n");
	printf("Options:\n");
	printf("  -d  --drop-buffer      Drop shared-memory buffer and exit\n");
	printf("  -k  --keep-buffer      Keep shared-memory after shutdown\n");
	printf("  -o  --old-buffer       Use existing shared-memory\n");
	printf("  -p, --port=N           TCP port for ADC stream (default: %u)\n", STREAM_PORT_DEFAULT);
	printf("  -h, --help             Show this help message\n\n");
	printf("Output format:\n");
	printf("  One CSV line per ring chunk, ADC 16-bit values only\n");
}

static int create_server_socket(uint16_t port)
{
	int fd;
	int yes = 1;
	struct sockaddr_in addr;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -errno;

	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
		int err = errno;
		close(fd);
		return -err;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);

	if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
		int err = errno;
		close(fd);
		return -err;
	}

	if (listen(fd, STREAM_BACKLOG) < 0) {
		int err = errno;
		close(fd);
		return -err;
	}

	return fd;
}

static int rpi_shm_create(struct shmem_info *shi, struct mvaring **mr)
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

static int open_existing_shm_wait(struct shmem_info *shi, struct mvaring **mr)
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

int main(int argc, char *argv[])
{
	struct shmem_info in = {0};
	struct mvaring *mr;
	int server_fd = -1;
	int client_fd = -1;
	int ret;
	int opt;

	static struct option long_options[] = {
		{"help",	no_argument,		NULL, 'h'},
		{"drop-buffer",	no_argument,		NULL, 'd'},
		{"keep-buffer",	no_argument,		NULL, 'k'},
		{"old-buffer",	no_argument,		NULL, 'o'},
		{"port",	required_argument,	NULL, 'p'},
		{NULL,           0,                 NULL, 0}
	};

	/* Parse command line arguments */
	while ((opt = getopt_long(argc, argv, "dhkop:", long_options, NULL)) != -1) {
		switch (opt) {
		case 'd':
			g_shm_drop = true;
			break;
		case 'k':
			g_keep_buffer = true;
			break;
		case 'o':
			g_use_old_buff = true;
			break;
		case 'p': {
			long port = strtol(optarg, NULL, 10);
			if (port < 1 || port > 65535) {
				fprintf(stderr, "Invalid port: %s\n", optarg);
				return EINVAL;
			}
			g_stream_port = (uint16_t)port;
			break;
		}
		case 'h':
			print_usage(argv[0]);
			return 0;
		default:
			fprintf(stderr, "Use -h for help\n");
			return 1;
		}
	}

	if (g_shm_drop && g_keep_buffer) {
		fprintf(stderr, "Error: Can't keep and drop buffer (-k and -d)\n");
		print_usage(argv[0]);

		return EINVAL;
	}

	/* Create or open shared memory area */
	if (g_use_old_buff)
		ret = open_existing_shm_wait(&in, &mr);
	else
		ret = rpi_shm_create(&in, &mr);

	/*
	 * The shmem_open() or rpi_shm_create() should've mapped SHM.
	 * We just need to do normal clean-up and exit
	 */
	if (g_shm_drop)
		goto out;

	if (ret) {
		printf("Nooo\n");
		return ret;
	}

	server_fd = create_server_socket(g_stream_port);
	if (server_fd < 0) {
		ret = -server_fd;
		fprintf(stderr, "Failed to create stream socket on port %u: %s\n",
			g_stream_port, strerror(ret));
		printf("Ou1\n");
		goto out;
	}

	printf("ADC stream socket listening on port %u\n", g_stream_port);

	for (;;) {
		if (client_fd < 0) {
			client_fd = accept(server_fd, NULL, NULL);
			if (client_fd < 0) {
				if (errno == EINTR)
					continue;
				ret = errno;
				fprintf(stderr, "accept failed: %s\n", strerror(ret));
				printf("Ou2\n");
				goto out;
			}
			printf("ADC stream client connected\n");
		}

		if (!mr || !ring_is_ok(mr)) {			
			fprintf(stderr, "Ring not ready, waiting for producer...\n");
			shmem_close_and_reset(&in);
			ret = open_existing_shm_wait(&in, &mr);
			if (ret){
				printf("Ou3\n");
				goto out;
			}
			continue;
		}

		ret = ring_read(mr, &data[0], ARRAY_SIZE(data));		
		if (ret == 0 || ret == -EAGAIN) {
			usleep(1000);			
			continue;
		}
		if (ret < 0) {
			printf("Here4\n");
			fprintf(stderr, "ring_read error %d, retrying\n", ret);
			usleep(10000);
			continue;
		}

		for (int i = 0; i < ret; i++) {			
			ssize_t line_len = format_adc_csv(&data[i], g_stream_line, sizeof(g_stream_line));			
			if (line_len < 0) {				
				fprintf(stderr, "Failed to format ADC stream line\n");
				ret = EOVERFLOW;				
				goto out;
			}			
			if (send_all(client_fd, g_stream_line, (size_t)line_len) < 0) {				
				close(client_fd);
				client_fd = -1;
				printf("ADC stream client disconnected\n");
				break;				
			}			
		}
	}

out:
	if (client_fd >= 0)
		close(client_fd);
	if (server_fd >= 0)
		close(server_fd);
	if (!g_keep_buffer && in.buff)
		shmem_destroy(&in);

	return ret;
}
