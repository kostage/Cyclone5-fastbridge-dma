#ifndef __PLNG_DMA_DRV_H__
#define __PLNG_DMA_DRV_H__

#include <linux/types.h>
#include <linux/mm_types.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/dmaengine.h>
#include <linux/miscdevice.h>

#include "rlsctl.h"

#define IOBUF_SIZE (BUF_MAX_SIZE)

struct plng_dma_device {
	void *buf;
        dma_addr_t dma_buf;
	void __iomem *base;
	dma_addr_t dma_base;
	unsigned base_size;
	struct dma_chan *dmach;

	unsigned long dma_mode;
	unsigned long fifo_mode;

	struct completion transfer_ok;
	struct vm_area_struct *usr_vma;

	struct miscdevice mdev;
	struct platform_device *pdev;
	void (*dma_callback)(void*);
};

#endif // __PLNG_DMA_DRV_H__x
