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

#define	MAX_FILE_OFFSET		(400ULL * 1024ULL * 1024ULL * 1024ULL)
#define	BLOCK_SIZE		(4ULL * 1024ULL)
#define	MAX_OUTSTANDING_IO	256
#define	MAX_SUBMIT_LOOP		256
#define	NUM_KEVENT		16

#define	DO_DEBUG		0

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

	a->buf = calloc(1, len);
	if (a->buf == NULL) {
		warn("%s: malloc", __func__);
		free(a);
		return (NULL);
	}

	a->aio.aio_fildes = fd;
	a->aio.aio_nbytes = len;
	a->aio.aio_offset = offset;
	a->aio.aio_buf = a->buf;

#if DO_DEBUG
	printf("%s: op %p: offset %lld, len %lld\n", __func__, a, (long long) offset, (long long) len);
#endif

	TAILQ_INSERT_TAIL(&aio_op_list, a, node);

	return (a);
}

void
aio_op_free(struct aio_op *a)
{

#if DO_DEBUG
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
#if DO_DEBUG
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

int
main(int argc, const char *argv[])
{
	int fd;
	int submitted = 0;
	struct aio_op *a;
	off_t o;
	int r, n, i;
	struct timespec tv;
	struct aiocb *aio;
	int kq_fd;
	struct kevent kev_list[NUM_KEVENT];

	fd = open("/dev/ada0", O_RDONLY | O_DIRECT);
	if (fd < 0) {
		err(1, "%s: open\n", __func__);
	}

	TAILQ_INIT(&aio_op_list);

	kq_fd = kqueue();

	/*
	 * Begin running
	 */
	for (;;) {
		/*
		 * Submit how many we need up to MAX_OUTSTANDING_IO..
		 */
		for (i = 0; i < MAX_SUBMIT_LOOP && submitted < MAX_OUTSTANDING_IO; i++) {
			/*
			 * XXX yes, this could be done by masking off the bits that
			 * represent BLOCK_SIZE..
			 */
			o = random() % (MAX_FILE_OFFSET / BLOCK_SIZE);
			o *= BLOCK_SIZE;

			a = aio_op_create(fd, o, BLOCK_SIZE);
			if (a != NULL) {
#if DO_DEBUG
				printf("%s: op %p: submitting\n", __func__, a);
#endif

				/*
				 * XXX need to fill in the sigevent section of aiocb
				 * XXX with the kqueue specific bits.
				 */
				a->aio.aio_sigevent.sigev_notify_kqueue = kq_fd;
				a->aio.aio_sigevent.sigev_notify = SIGEV_KEVENT;
				a->aio.aio_sigevent.sigev_value.sigval_ptr = a;

				/*
				 * Then, we can register for aio read.
				 */
				r = aio_read(&a->aio);
				if (r != 0) {
					/*
					 * Ideally we'd just queue the request locally, put
					 * some back-pressure on queuing any further IO
					 * and restart the IO queue when we've completed
					 * things.  This, however, is just for testing.
					 */
					printf("%s: op %p: failed; errno=%d (%s)\n", __func__, a, errno, strerror(errno));
					aio_op_free(a);
					break;
				}
				submitted++;
			}
		}

		while (submitted > 0) {
#if 0
			/*
			 * Now, handle completions; 100ms timeout.
			 */
			tv.tv_sec = 0;
			tv.tv_nsec = 100 * 1000;
			r = aio_waitcomplete(&aio, &tv);
			if (r < 0) {
//				fprintf(stderr, "%s: timeout hit?\n", __func__);
				break;
			}
			aio_op_complete_aio(aio);
			if (submitted == 0)
				fprintf(stderr, "%s: huh? freed event, but submit=0?\n", __func__);
			else
				submitted--;
#endif
			tv.tv_sec = 0;
			tv.tv_nsec = 100 * 1000;
#if DO_DEBUG
			printf("%s: submitted=%d; calling kevent\n", __func__, submitted);
#endif
			n = kevent(kq_fd, NULL, 0, kev_list, NUM_KEVENT, &tv);

			/* n == 0 equals 'timeout' */
			if (n == 0)
				break;

			if (n < 0) {
				warn("%s: kevent (completion)", __func__);
				break;
			}
#if DO_DEBUG
			printf("%s: %d events ready\n", __func__, n);
#endif

			/* Walk the list; look for completion information */
			for (i = 0; i < n; i++) {
				r = aio_return((struct aiocb *) kev_list[i].ident);

				/* IO completed with error! */
				if (r < 0) {
					warn("%s: op %p: aio_return", __func__, (void *) kev_list[i].udata);
				}

				/* If it's >= 0 then it's the IO completion size */

				/*
				 * XXX should ensure somehow that we're lining up the aiocb and
				 * XXX aio data pointers all correctly!
				 *
				 * XXX the ident field will be set to aiocb, so we can do some
				 * XXX basic validation!
				 */
				aio_op_complete((struct aio_op *) kev_list[i].udata, (struct aiocb *) kev_list[i].ident);
				submitted--;
			}
		}
#if DO_DEBUG
//		sleep(1);
#endif
	}

	close(fd);
	exit(0);
}

