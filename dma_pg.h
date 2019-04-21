/**
 * @file:	fourier_pg.h
 * @version:	1.0.0
 * @date:	16 Feb 2017
 */

#if !defined(DMA_PG_H)
#define DMA_PG_H

#include <linux/types.h>
#include "plng_dma_device.h"

ssize_t dma_read_pg(struct plng_dma_device *dma_dev,
		    void __user * dst,
		    const loff_t br_offset,
		    size_t count);

ssize_t dma_write_pg(struct plng_dma_device * dma_dev,
		     const void __user * src,
		     loff_t br_offset,
		     size_t count);

#endif /* !defined(DMA_PG_H) */
