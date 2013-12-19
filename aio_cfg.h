#ifndef	__AIO_CFG_H__
#define	__AOP_CFG_H__

/* Debug printing */
#define	AIO_DO_DEBUG		0


/*
 * How many entries to submit each loop before we try
 * reaping some.
 */
#define	MAX_SUBMIT_LOOP		256

/*
 * How many completions to handle per kevent() call.
 */
#define	NUM_KEVENT		32

/* Maximum number of aio_disk entries we'll support */
#define	MAX_AIO_DISKS		128

#endif	/* __AIO_CFG_H__ */
