#ifndef	__AIO_DISK_H__
#define	__AIO_DISK_H__

struct aio_disk {
	int fd;
	const char *pathname;
	size_t file_size;
	size_t block_size;
};

#endif	/* __AIO_DISK_H__ */
