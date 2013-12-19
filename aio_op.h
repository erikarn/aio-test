#ifndef	__AIO_OP_H__
#define	__AIO_OP_H__

struct aio_op {
	struct aiocb aio;
	TAILQ_ENTRY(aio_op) node;
	char *buf;
	size_t bufsize;
};

#endif	/* __AIO_OP_H__ */
