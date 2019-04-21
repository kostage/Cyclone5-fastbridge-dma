/**
 * @file:	rlsctl.h
 * @version:	1.0.3
 * @date:	14 Feb 2017
 */

#if !defined(DMADRV_H)
#define	DMADRV_H

#include <linux/types.h>
#include <linux/ioctl.h>

enum {
  DUMB_OPMODE = 0,
  DMA_OPMODE,
  DMAPG_OPMODE,
  INVALID_OPMODE
};

enum {
  INCR_ADDR = 0,
  FIFO_ADDR,
  INVALID_ADDR
};

#define BUF_MAX_SIZE (4U*1024U*1024U)
#define IOBUF_SIZE (BUF_MAX_SIZE)

#define FAST_BRIDGE       (0U)
#define SLOW_BRIDGE       (1U)

#define DMADRV_IOC_MAGIC	(0xe7U)
#define OPMODE        		(3U)
#define BRIDGE         		(5U)
#define INCRADDR       		(7U)

#define _IORB(type,nr,size)	_IOC(_IOC_READ,(type),(nr),(size))
#define _IOWB(type,nr,size)	_IOC(_IOC_WRITE,(type),(nr),(size))

#define DMADRV_SETOPMODE    	_IOWB(DMADRV_IOC_MAGIC, OPMODE,   0)
#define DMADRV_GETOPMODE    	_IORB(DMADRV_IOC_MAGIC, OPMODE,   0)
#define DMADRV_SETBRIDGE    	_IOWB(DMADRV_IOC_MAGIC, BRIDGE,   0)
#define DMADRV_GETBRIDGE   	_IORB(DMADRV_IOC_MAGIC, BRIDGE,   0)
#define DMADRV_SETINCRADDR  	_IOWB(DMADRV_IOC_MAGIC, INCRADDR, 0)
#define DMADRV_GETINCRADDR   	_IORB(DMADRV_IOC_MAGIC, INCRADDR, 0)

#endif /* !defined(DMADRV_H) */
