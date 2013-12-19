#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <aio.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/queue.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/disk.h>

#include "aio_cfg.h"

#include "aio_op.h"

/*
 * This is a global list of AIO operations.
 *
 * It may be interesting to track these per-device rather
 * than global.  But, that's a later thing.
 */
TAILQ_HEAD(, aio_op) aio_op_list;

struct aio_op *
aio_op_create(int fd, off_t offset, size_t len)
{
	struct aio_op *a;

	a = calloc(1, sizeof(*a));
	if (a == NULL) {
		warn("%s: calloc", __func__);
		return (NULL);
	}

	a->buf = malloc(len);
	if (a->buf == NULL) {
		warn("%s: malloc", __func__);
		free(a);
		return (NULL);
	}

	a->aio.aio_fildes = fd;
	a->aio.aio_nbytes = len;
	a->aio.aio_offset = offset;
	a->aio.aio_buf = a->buf;

#if AIO_DO_DEBUG
	printf("%s: op %p: offset %lld, len %lld\n", __func__, a, (long long) offset, (long long) len);
#endif

	TAILQ_INSERT_TAIL(&aio_op_list, a, node);

	return (a);
}

void
aio_op_free(struct aio_op *a)
{

#if AIO_DO_DEBUG
	printf("%s: op %p: freeing\n", __func__, a);
#endif

	if (a->buf)
		free(a->buf);
	TAILQ_REMOVE(&aio_op_list, a, node);
	free(a);
}

int
aio_op_complete_aio(struct aiocb *aio)
{
	struct aio_op *a, *an;

	TAILQ_FOREACH_SAFE(a, &aio_op_list, node, an) {
		if (aio == &a->aio) {
#if AIO_DO_DEBUG
			printf("%s: op %p: completing\n", __func__, a);
#endif
			aio_op_free(a);
			return (0);
		}
	}

	fprintf(stderr, "%s: couldn't find it!\n", __func__);
	return (-1);
}

void
aio_op_complete(struct aio_op *a, struct aiocb *aio)
{
	if (&a->aio != aio) {
		printf("%s: a.aio != aio!\n", __func__);
	}
	aio_op_free(a);
}

void
aio_op_init(void)
{

	TAILQ_INIT(&aio_op_list);
}
