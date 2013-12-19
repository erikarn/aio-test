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
 * Setup (but do not open) an aio_disk structure.
 */
int
aio_disk_init(struct aio_disk *aiod, const char *pathname, size_t block_size)
{

	aiod->fd = -1;
	aiod->file_size = 0;
	aiod->pathname = strdup(pathname);
	aiod->block_size = block_size;

	return (0);
}

/*
 * Open up an exisiting aio disk.  returns 0 on OK, -1 and errno set on error.
 */
int
aio_disk_open(struct aio_disk *aiod)
{
	off_t disk_size;

	if (aiod->fd != -1)
		close(aiod->fd);

	aiod->fd = open(aiod->pathname, O_RDONLY | O_DIRECT);
	if (aiod->fd < 0) {
		warn("%s: open (%s)", __func__, aiod->pathname);
		return (-1);
	}

	if (ioctl(aiod->fd, DIOCGMEDIASIZE, &disk_size, sizeof(disk_size)) < 0) {
		close(aiod->fd);
		aiod->fd = -1;
		warn("%s: (%s): ioctl(DIOCGMEDIASIZE)", __func__, aiod->pathname);
		return (-1);
	}

	aiod->file_size = disk_size;

	printf("%s: (%s): size=%llu, block_size=%llu\n",
	    __func__,
	    aiod->pathname,
	    (unsigned long long) aiod->file_size,
	    (unsigned long long) aiod->block_size);

	return (0);
}

/*
 * Open up an exisiting aio file.  returns 0 on OK, -1 and errno set on error.
 */
int
aio_file_open(struct aio_disk *aiod)
{
	struct stat sb;

	if (aiod->fd != -1)
		close(aiod->fd);

	aiod->fd = open(aiod->pathname, O_RDONLY | O_DIRECT);
	if (aiod->fd < 0) {
		warn("%s: open (%s)", __func__, aiod->pathname);
		return (-1);
	}

	if (fstat(aiod->fd, &sb) < 0) {
		warn("%s: (%s): fstat", __func__, aiod->pathname);
		return (-1);
	}

	aiod->file_size = sb.st_size;

	printf("%s: (%s): size=%llu, block_size=%llu\n",
	    __func__,
	    aiod->pathname,
	    (unsigned long long) aiod->file_size,
	    (unsigned long long) aiod->block_size);

	return (0);
}

int
aio_disk_close(struct aio_disk *aiod)
{

	if (aiod->fd != -1)
		close(aiod->fd);
	aiod->fd = -1;

	return (0);
}
