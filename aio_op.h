#ifndef	__AIO_OP_H__
#define	__AIO_OP_H__

struct aio_op {
	struct aiocb aio;
	TAILQ_ENTRY(aio_op) node;
	char *buf;
	size_t bufsize;
};

extern	struct aio_op * aio_op_create(int fd, off_t offset, size_t len);
extern	void aio_op_free(struct aio_op *a);
extern	int aio_op_complete_aio(struct aiocb *aio);
extern	void aio_op_complete(struct aio_op *a, struct aiocb *aio);
extern void aio_op_init(void);

#endif	/* __AIO_OP_H__ */
