/* mvaring_ext.c - Python C extension for reading mvaring ring buffer from shared memory
 *
 * Exposes a minimal read-only consumer API:
 *   mvaring.open(shm_name)         -> opaque handle (capsule)
 *   mvaring.close(handle)          -> None
 *   mvaring.is_ok(handle)          -> bool
 *   mvaring.available(handle)      -> int
 *   mvaring.read(handle, max_chunks) -> list[dict]
 *
 * Each dict in the returned list has:
 *   'usecs'    : int              - timestamp in microseconds
 *   'samples'  : bytes            - MAX_SAMPS uint32_t values (raw SPI words)
 *   'gpio_lev0': bytes            - MAX_SAMPS uint32_t GPIO level snapshots
 *
 * Use array.array('I').frombytes(chunk['samples']) to get a list of uint32 values.
 *
 * NOTE: ring_read() advances rindex in shared memory, so this is a proper
 * consuming reader. Only ONE reader process should consume from the ring
 * at a time; multiple concurrent readers will corrupt rindex.
 */

#define _GNU_SOURCE
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "../mvaring.h"
#include "../rpi_shmem.h"

typedef struct {
	struct shmem_info shmem;
	struct mvaring *ring;
} mvaring_ctx_t;

/* Called by Python GC when the capsule is collected (or after explicit close). */
static void ctx_destructor(PyObject *capsule)
{
	mvaring_ctx_t *ctx = (mvaring_ctx_t *)PyCapsule_GetPointer(capsule, "mvaring.ctx");
	if (!ctx)
		return;

	/* shmem_close is a no-op when buff is NULL (already closed manually) */
	shmem_close(&ctx->shmem);
	free((void *)ctx->shmem.name);
	free(ctx);
}

/* mvaring.open(shm_name: str) -> handle
 *
 * Opens the POSIX shared memory segment and validates that it contains a
 * well-formed mvaring ring buffer.  The shm_name must start with '/'
 * (e.g. "/RPI_ADC_BUFF").
 *
 * Raises OSError on shm_open/mmap failure, ValueError on bad ring buffer.
 */
static PyObject *py_mvaring_open(PyObject *self, PyObject *args)
{
	const char *name;

	if (!PyArg_ParseTuple(args, "s", &name))
		return NULL;

	mvaring_ctx_t *ctx = (mvaring_ctx_t *)calloc(1, sizeof(*ctx));
	if (!ctx)
		return PyErr_NoMemory();

	/*
	 * Open read-write: ring_read() must advance rindex in shared memory.
	 * From the Python caller's perspective this is still "read-only" —
	 * only ADC data is read, never written.
	 */
	int ret = shmem_open(name, sizeof(struct mvaring), &ctx->shmem, /*quiet=*/false);
	if (ret < 0) {
		free(ctx);
		errno = -ret;
		return PyErr_SetFromErrnoWithFilename(PyExc_OSError, name);
	}

	ctx->ring = (struct mvaring *)ctx->shmem.buff;

	if (!ring_is_ok(ctx->ring)) {
		shmem_close(&ctx->shmem);
		free((void *)ctx->shmem.name);
		free(ctx);
		PyErr_Format(PyExc_ValueError,
			"Shared memory '%s' does not contain a valid mvaring ring buffer "
			"(expected version %d, size %zu)",
			name, MVARING_VERSION, sizeof(struct mvaring));
		return NULL;
	}

	return PyCapsule_New(ctx, "mvaring.ctx", ctx_destructor);
}

/* mvaring.close(handle) -> None
 *
 * Explicitly releases the shared memory mapping.  The handle becomes invalid
 * after this call; subsequent use raises ValueError.
 */
static PyObject *py_mvaring_close(PyObject *self, PyObject *args)
{
	PyObject *capsule;

	if (!PyArg_ParseTuple(args, "O", &capsule))
		return NULL;

	mvaring_ctx_t *ctx = (mvaring_ctx_t *)PyCapsule_GetPointer(capsule, "mvaring.ctx");
	if (!ctx) {
		PyErr_SetString(PyExc_ValueError, "Invalid or already-closed mvaring handle");
		return NULL;
	}

	shmem_close(&ctx->shmem);
	free((void *)ctx->shmem.name);

	/* Zero out pointers so the GC destructor is a safe no-op */
	ctx->shmem.buff = NULL;
	ctx->shmem.name = NULL;
	ctx->ring = NULL;

	Py_RETURN_NONE;
}

/* mvaring.is_ok(handle) -> bool
 *
 * Returns True if the ring buffer header looks valid (correct version + size).
 */
static PyObject *py_mvaring_is_ok(PyObject *self, PyObject *args)
{
	PyObject *capsule;

	if (!PyArg_ParseTuple(args, "O", &capsule))
		return NULL;

	mvaring_ctx_t *ctx = (mvaring_ctx_t *)PyCapsule_GetPointer(capsule, "mvaring.ctx");
	if (!ctx) {
		PyErr_SetString(PyExc_ValueError, "Invalid or already-closed mvaring handle");
		return NULL;
	}

	return PyBool_FromLong(ring_is_ok(ctx->ring));
}

/* mvaring.available(handle) -> int
 *
 * Returns the number of chunks currently available for reading.
 * This is a snapshot; the value may change immediately due to the writer.
 */
static PyObject *py_mvaring_available(PyObject *self, PyObject *args)
{
	PyObject *capsule;

	if (!PyArg_ParseTuple(args, "O", &capsule))
		return NULL;

	mvaring_ctx_t *ctx = (mvaring_ctx_t *)PyCapsule_GetPointer(capsule, "mvaring.ctx");
	if (!ctx) {
		PyErr_SetString(PyExc_ValueError, "Invalid or already-closed mvaring handle");
		return NULL;
	}

	return PyLong_FromUnsignedLong(ring_available(ctx->ring));
}

/* mvaring.read(handle, max_chunks: int = 64) -> list[dict]
 *
 * Reads up to max_chunks entries from the ring buffer, advancing rindex.
 * Returns an empty list when no data is available (non-blocking).
 *
 * Each entry dict:
 *   'usecs'    : int   - writer timestamp (microseconds)
 *   'samples'  : bytes - MAX_SAMPS * 4 bytes (array of uint32_t, raw SPI words)
 *   'gpio_lev0': bytes - MAX_SAMPS * 4 bytes (array of uint32_t, GPIO states)
 *
 * To decode samples as unsigned ints:
 *   import array
 *   vals = array.array('I')
 *   vals.frombytes(chunk['samples'])
 *
 * Raises OSError on unexpected error (-EINVAL from ring_read).
 */
static PyObject *py_mvaring_read(PyObject *self, PyObject *args)
{
	PyObject *capsule;
	unsigned int max_chunks = 64;

	if (!PyArg_ParseTuple(args, "O|I", &capsule, &max_chunks))
		return NULL;

	mvaring_ctx_t *ctx = (mvaring_ctx_t *)PyCapsule_GetPointer(capsule, "mvaring.ctx");
	if (!ctx) {
		PyErr_SetString(PyExc_ValueError, "Invalid or already-closed mvaring handle");
		return NULL;
	}

	if (max_chunks == 0 || max_chunks > NUM_DATA_CHUNKS)
		max_chunks = NUM_DATA_CHUNKS;

	struct adc_data *buf = (struct adc_data *)malloc(max_chunks * sizeof(struct adc_data));
	if (!buf)
		return PyErr_NoMemory();

	int n = ring_read(ctx->ring, buf, max_chunks);

	if (n == -EAGAIN) {
		free(buf);
		return PyList_New(0);  /* no data available — non-blocking return */
	}

	if (n < 0) {
		free(buf);
		errno = -n;
		return PyErr_SetFromErrno(PyExc_OSError);
	}

	PyObject *result = PyList_New(n);
	if (!result) {
		free(buf);
		return NULL;
	}

	for (int i = 0; i < n; i++) {
		PyObject *chunk    = PyDict_New();
		PyObject *usecs    = PyLong_FromUnsignedLong(buf[i].usecs);
		PyObject *samples  = PyBytes_FromStringAndSize(
					(const char *)buf[i].samples,
					MAX_SAMPS * sizeof(uint32_t));
		PyObject *gpio     = PyBytes_FromStringAndSize(
					(const char *)buf[i].gpio_lev0,
					MAX_SAMPS * sizeof(uint32_t));

		if (!chunk || !usecs || !samples || !gpio) {
			Py_XDECREF(chunk);
			Py_XDECREF(usecs);
			Py_XDECREF(samples);
			Py_XDECREF(gpio);
			Py_DECREF(result);
			free(buf);
			return PyErr_NoMemory();
		}

		/* SetItemString increments ref-count of value; we own a ref we must release */
		PyDict_SetItemString(chunk, "usecs",     usecs);
		PyDict_SetItemString(chunk, "samples",   samples);
		PyDict_SetItemString(chunk, "gpio_lev0", gpio);
		Py_DECREF(usecs);
		Py_DECREF(samples);
		Py_DECREF(gpio);

		PyList_SET_ITEM(result, i, chunk);  /* steals reference to chunk */
	}

	free(buf);
	return result;
}

/* -------------------------------------------------------------------------- */

static PyMethodDef mvaring_methods[] = {
	{"open",      py_mvaring_open,      METH_VARARGS,
		"open(shm_name) -> handle\n\n"
		"Open a POSIX shared memory segment and attach to the mvaring ring buffer.\n"
		"shm_name must start with '/' (e.g. '/RPI_ADC_BUFF')."},

	{"close",     py_mvaring_close,     METH_VARARGS,
		"close(handle) -> None\n\nRelease the shared memory mapping."},

	{"is_ok",     py_mvaring_is_ok,     METH_VARARGS,
		"is_ok(handle) -> bool\n\nReturn True if the ring buffer header is valid."},

	{"available", py_mvaring_available, METH_VARARGS,
		"available(handle) -> int\n\nNumber of chunks currently available for reading."},

	{"read",      py_mvaring_read,      METH_VARARGS,
		"read(handle, max_chunks=64) -> list[dict]\n\n"
		"Read up to max_chunks entries from the ring buffer (consuming).\n"
		"Returns an empty list when no data is available.\n"
		"Each dict: {'usecs': int, 'samples': bytes, 'gpio_lev0': bytes}"},

	{NULL, NULL, 0, NULL}
};

static struct PyModuleDef mvaring_module = {
	PyModuleDef_HEAD_INIT,
	"mvaring",
	"Python bindings for the mvaring lock-free ring buffer (read-only consumer).",
	-1,
	mvaring_methods
};

PyMODINIT_FUNC PyInit_mvaring(void)
{
	PyObject *m = PyModule_Create(&mvaring_module);
	if (!m)
		return NULL;

	/* Expose compile-time constants so Python code can use them */
	PyModule_AddIntConstant(m, "MAX_SAMPS",       MAX_SAMPS);
	PyModule_AddIntConstant(m, "NUM_DATA_CHUNKS",  NUM_DATA_CHUNKS);
	PyModule_AddIntConstant(m, "MVARING_VERSION",  MVARING_VERSION);

	return m;
}
