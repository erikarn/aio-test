#ifndef	__AIO_DISK_H__
#define	__AIO_DISK_H__

struct aio_disk {
	int fd;
	const char *pathname;
	size_t file_size;
	size_t block_size;
};

extern	int aio_disk_init(struct aio_disk *, const char *, size_t);
extern	int aio_disk_open(struct aio_disk *);
extern	int aio_file_open(struct aio_disk *);
extern	int aio_disk_close(struct aio_disk *);

#endif	/* __AIO_DISK_H__ */
