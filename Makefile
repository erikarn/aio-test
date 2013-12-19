NO_MAN=	1

.include <bsd.own.mk>

PROG=	aio-test
SRCS=	aio-test.c aio_disk.c aio_op.c
CFLAGS+=	-O -g -ggdb

.include <bsd.prog.mk>
