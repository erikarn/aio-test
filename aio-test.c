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
#include "aio_disk.h"

/*
 * We've had an error from lio_listio().
 *
 * Find which requests were submitted and which are invalid.
 *
 * XXX since we're submitting these via kqueue, then we should
 * XXX already have a cached copy of the local state
 * XXX (in aio->aio_sigevent.sigev_value.sigval_ptr.)
 */
int
aio_tidyup_listio(struct aiocb *lio_aio[], int nlio)
{
	int i;
	int r, s;
	for (i = 0; i < nlio; i++) {
		r = aio_error(lio_aio[i]);
		/* In progress? It's in queue, not completed */
		if (r == EINPROGRESS)
			continue;

		/* returns -1 and errno==EINVAL? Then this wasn't queued */
		if (r == -1 && errno == EINVAL) {
			fprintf(stderr, "%s: aio %p: wasn't queued\n", __func__, lio_aio[i]);
			aio_op_complete_aio(lio_aio[i]);
			continue;
		}

		/*
		 * returned 0? Then it's already completed successfully.
		 */ 
		if (r == 0) {
			s = aio_return(lio_aio[i]);
			fprintf(stderr, "%s: aio %p: completed successfully; return=%d!\n", __func__, lio_aio[i], s);
			aio_op_complete_aio(lio_aio[i]);
			continue;
		}

		/*
		 * Anything else is an AIO that completed unsuccessfully, so call aio_return()
		 * and then complete it.
		 */
		s = aio_return(lio_aio[i]);
		fprintf(stderr, "%s: aio %p: completed unsuccessfully; r=%d, return=%d\n", __func__, lio_aio[i], r, s);
		aio_op_complete_aio(lio_aio[i]);
	}

	return (0);
}

int
main(int argc, const char *argv[])
{
	int submitted = 0;
	struct aio_op *a;
	off_t o;
	int r, n, i;
	struct timespec tv;
	struct aiocb *aio;
	int kq_fd;
	struct kevent kev_list[NUM_KEVENT];
	struct aio_disk *aiod;
	int num_aiod = 0;
	struct aio_disk *ad;
	int block_size, max_outstanding_io;
	int nlio = 0;
	struct aiocb *lio_aio[AIO_LISTIO_MAX];

	aio_op_init();

	/*
	 * Allocate some aio disk entries to use.
	 */
	aiod = calloc(MAX_AIO_DISKS, sizeof(struct aio_disk));
	if (aiod == NULL)
		err(1, "calloc");

	/*
	 * Parse the command line arguments, creating aio_disk entries
	 * as appropriate.
	 */

	/* First argument, is the block size */
	/* XXX use getopt! */
	i = 1;	/* argv[0] is the program name, after all */
	block_size = atoi(argv[i]);
	i++;

	/* Next is the maximum outstanding IO */
	max_outstanding_io = atoi(argv[i]);
	i++;

	/*
	 * The rest are disks.
	 */
	for (; i < argc && num_aiod < MAX_AIO_DISKS; i++) {
		(void) aio_disk_init(&aiod[num_aiod], argv[i], block_size);
		num_aiod++;
	}

	/* Now, open each */
	for (i = 0; i < num_aiod; i++) {
		if (aio_disk_open(&aiod[i]) < 0) {
			if (aio_file_open(&aiod[i]) < 0) {
				exit(1);
			}
		}
	}

	/*
	 * Ok, now we know the set of files that we're going to be
	 * operating on.  Go through and open each of them in turn.
	 */

	kq_fd = kqueue();

	/*
	 * Begin running
	 */
	for (;;) {
		/*
		 * Submit how many we need up to MAX_OUTSTANDING_IO..
		 */
		for (i = 0; i < MAX_SUBMIT_LOOP && submitted < max_outstanding_io; i++) {
			/*
			 * Pick a random disk.
			 */
			ad = &aiod[random() % num_aiod];
			
			/*
			 * XXX yes, this could be done by masking off the bits that
			 * represent BLOCK_SIZE..
			 */
			o = random() % (ad->file_size / ad->block_size);
			o *= ad->block_size;

			a = aio_op_create(ad->fd, o, ad->block_size);
			if (a != NULL) {
#if AIO_DO_DEBUG
				printf("%s: op %p: submitting\n", __func__, a);
#endif

				/*
				 * XXX need to fill in the sigevent section of aiocb
				 * XXX with the kqueue specific bits.
				 */
				a->aio.aio_sigevent.sigev_notify_kqueue = kq_fd;
				a->aio.aio_sigevent.sigev_notify = SIGEV_KEVENT;
				a->aio.aio_sigevent.sigev_value.sigval_ptr = a;

				/* We're a read */
				a->aio.aio_lio_opcode = LIO_READ;

				/* Whether or not we succeed - bump submitted. */
				submitted++;
				/* Add to the listio list */
				lio_aio[nlio] = &a->aio;
				nlio ++;
				if (nlio < AIO_LISTIO_MAX)
					continue;
				/*
				 * Ok, we've filled our listio; so submit.
				 */
				r = lio_listio(LIO_NOWAIT, lio_aio, nlio, NULL);
				if (r < 0) {
					/* Error */
					warn("%s: lio_listio", __func__);
					aio_tidyup_listio(lio_aio, nlio);
					nlio = 0;
					continue;
				}

				/* ok, go back to the beginning */
				nlio = 0;
			}
		}

		/*
		 * Submit whatever's left over.
		 */
		r = lio_listio(LIO_NOWAIT, lio_aio, nlio, NULL);
		if (r < 0) {
			/* Error */
			warn("%s: lio_listio", __func__);
			aio_tidyup_listio(lio_aio, nlio);
		}

		/* ok, go back to the beginning regardless of success or failure */
		nlio = 0;

		/* Now, handle completions */

		while (submitted > 0) {
			/*
			 * Wait up to 1000ms.
			 */
			tv.tv_sec = 0;
			tv.tv_nsec = 1000 * 1000;
#if AIO_DO_DEBUG
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
#if AIO_DO_DEBUG
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
	}

	/*
	 * Close files
	 */
	for (i = 0; i < num_aiod; i++)
		aio_disk_close(&aiod[i]);
	exit(0);
}

