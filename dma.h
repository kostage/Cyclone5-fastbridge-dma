/**
 * @file:	fourier.h
 * @version:	1.0.0
 * @date:	16 Feb 2017
 */

#if !defined(DMA_H)
#define DMA_H

#include <linux/types.h>
#include "plng_dma_device.h"

ssize_t dma_read(struct plng_dma_device *dma_dev,
		 void __user * dst,
		 const loff_t br_offset,
		 size_t count);

ssize_t dma_write(struct plng_dma_device * dma_dev,
		  const void __user * src,
		  loff_t br_offset,
		  size_t count);

int dma_mmap(struct plng_dma_device * dma_dev,
		 struct file *filp,
		 struct vm_area_struct *vma);

int dma_init(struct plng_dma_device *dma_dev);
void dma_fini(struct plng_dma_device *dma_dev);

#endif /* !defined(DMA_H) */
