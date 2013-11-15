#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <aio.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/queue.h>

#define	MAX_FILE_OFFSET		(10 * 1024 * 1024 * 1024)
#define	BLOCK_SIZE		(128 * 1024)
#define	MAX_OUTSTANDING_IO	512

struct aio_op {
	struct aiocb aio;
	TAILQ_ENTRY(aio_op) node;
	char *buf;
	int bufsize;
};

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

	printf("%s: op %p: offset %lld, len %lld\n", __func__, a, (long long) offset, (long long) len);

	TAILQ_INSERT_TAIL(&aio_op_list, a, node);

	return (a);
}

void
aio_op_free(struct aio_op *a)
{

	printf("%s: op %p: freeing\n", __func__, a);

	if (a->buf)
		free(a->buf);
	TAILQ_REMOVE(&aio_op_list, a, node);
	free(a);
}

int
aio_op_complete(struct aiocb *aio)
{
	struct aio_op *a, *an;

	TAILQ_FOREACH_SAFE(a, &aio_op_list, node, an) {
		if (aio == &a->aio) {
			printf("%s: op %p: completing\n", __func__, a);
			aio_op_free(a);
			return (0);
		}
	}

	fprintf(stderr, "%s: couldn't find it!\n", __func__);
	return (-1);
}

int
main(int argc, const char *argv[])
{
	int fd;
	int submitted = 0;
	struct aio_op *a;
	off_t o;
	int r;
	struct timespec tv;
	struct aiocb *aio;

	fd = open("/dev/ada0", O_RDONLY | O_DIRECT);
	if (fd < 0) {
		err(1, "%s: open\n", __func__);
	}

	TAILQ_INIT(&aio_op_list);

	/*
	 * Begin running
	 */
	for (;;) {
		/*
		 * Submit how many we need up to MAX_OUTSTANDING_IO..
		 */
		for (; submitted < MAX_OUTSTANDING_IO; submitted++) {
			/*
			 * XXX yes, this could be done by masking off the bits that
			 * represent BLOCK_SIZE..
			 */
			o = random() % (MAX_FILE_OFFSET / BLOCK_SIZE);
			o *= BLOCK_SIZE;

			a = aio_op_create(fd, o, BLOCK_SIZE);
			if (a != NULL) {
				printf("%s: op %p: submitting\n", __func__, a);
				r = aio_read(&a->aio);
				if (r != 0) {
					printf("%s: op %p: failed; errno=%d (%s)\n", __func__, a, errno, strerror(errno));
					aio_op_free(a);
					break;
				}
			}
		}

		for (;;) {
			/*
			 * Now, handle completions; 10ms timeout.
			 */
			tv.tv_sec = 0;
			tv.tv_nsec = 10 * 1000;
			r = aio_waitcomplete(&aio, &tv);
			if (r < 0) {
//				fprintf(stderr, "%s: timeout hit?\n", __func__);
				break;
			}
			aio_op_complete(aio);
			if (submitted == 0)
				fprintf(stderr, "%s: huh? freed event, but submit=0?\n", __func__);
			else
				submitted--;
		}
	}

	close(fd);
	exit(0);
}

