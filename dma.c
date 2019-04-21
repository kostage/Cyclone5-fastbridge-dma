/**
 * @file:	fourier.c
 * @version:	1.0.0
 * @date:	16 Feb 2017
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>

#include <linux/gfp.h>
#include <linux/uaccess.h>

#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>

#include "dma.h"
#include "log.h"
#include "khack.h"

/* buf must be within usr vma */
static dma_addr_t _translate_buf(struct plng_dma_device * dma_dev,
				 const void __user * buf,
				 size_t bcount)
{
	struct vm_area_struct *vma = dma_dev->usr_vma;
	size_t offset, cbuf = (size_t)buf;
	dma_addr_t dbuf;
	if (!vma)
		return 0;

	if (cbuf < vma->vm_start || cbuf >= vma->vm_end) {
		cbuf += bcount;
		if (cbuf < vma->vm_start || cbuf >= vma->vm_end)
			return 0;
	}

	offset = (size_t)buf - vma->vm_start;
	dbuf = dma_dev->dma_base + offset;

	return dbuf;
}

/**********************/
/******** READ ********/
/**********************/
ssize_t dma_read(struct plng_dma_device *dma_dev,
		 void __user * dst,
		 const loff_t br_offset,
		 size_t count)
{
	struct dma_async_tx_descriptor *desc;
	struct device *dev = &dma_dev->pdev->dev;
	struct scatterlist sg;
	struct dma_slave_config conf;
	dma_cookie_t cookie;

	/* Usr buf must be an alias to iodma buf */
	dma_addr_t ddst = _translate_buf(dma_dev, dst, count);
	if (!ddst) {
		dev_err(dev, "_translate_buf() error!\n");
		return -EINVAL;
	}

	/* Ensure CPU is done with reads */
	rmb();

	sg.offset = 0;
	sg.length = count;
	sg.dma_address = ddst;

	memzero_explicit(&conf, sizeof(struct dma_slave_config));
	conf.direction = DMA_DEV_TO_MEM;
	conf.src_addr = (phys_addr_t)(dma_dev->dma_base + br_offset);	
	conf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	conf.src_maxburst = 16;
	conf.dst_maxburst = 16;

	if (dmaengine_slave_config(dma_dev->dmach, &conf) < 0) {
		dev_err(dev, "dmaengine_slave_config() failure");
		return -EINVAL;
	}

	desc = dmaengine_prep_slave_sg(dma_dev->dmach,
				       &sg,
				       1,
				       DMA_DEV_TO_MEM,
				       DMA_INTERRUPT);

	if (IS_ERR(desc)) {
		dev_err(dev, "dmaengine_prep_slave_sg() failure\n");
		return -EIO;
	}

	dma_drv_hack_chdir(desc);

	init_completion(&dma_dev->transfer_ok);
	desc->callback = dma_dev->dma_callback;
	desc->callback_param = &dma_dev->transfer_ok;

	dma_drv_hack_setfifo(desc, DMA_DEV_TO_MEM);

	cookie = dmaengine_submit(desc);

	if (0 != dma_submit_error(cookie)) {
		dev_err(dev, "dma_submit() failure\n");
		return -EIO;
	}

	dma_async_issue_pending(dma_dev->dmach);
	dev_info(dev, "Start waiting...\n");

	wait_for_completion(&dma_dev->transfer_ok);

	/* CACHE SYNC HERE!!! */
/*
	dma_sync_single_for_cpu(dma_dev->dmach->device->dev,
				ddst,
				count,
				DMA_FROM_DEVICE);
*/
	return count;
}

/**********************/
/******* WRITE ********/
/**********************/
ssize_t dma_write(struct plng_dma_device * dma_dev,
		  const void __user * src,
		  loff_t br_offset,
		  size_t count)
{
	struct dma_async_tx_descriptor *desc;
	struct scatterlist sg;
	struct dma_slave_config conf;
	struct device *dev = &dma_dev->pdev->dev;
	dma_cookie_t cookie;

	/* Usr buf must be an alias to iodma buf */
	dma_addr_t dsrc = _translate_buf(dma_dev, src, count);
	if (!dsrc) {
		dev_err(dev, "_translate_buf() error!\n");
		return -EINVAL;
	}

	sg.offset = 0;
	sg.length = count;
	sg.dma_address = dsrc;

	memzero_explicit(&conf, sizeof(struct dma_slave_config));
	conf.direction = DMA_MEM_TO_DEV;
	conf.dst_addr = (phys_addr_t)(dma_dev->dma_base + br_offset);
	conf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	conf.src_maxburst = 16;
	conf.dst_maxburst = 16;

	if (dmaengine_slave_config(dma_dev->dmach, &conf) < 0) {
		dev_err(dev, "dmaengine_slave_config() failure\n");
		return -EINVAL;
	}

	desc = dmaengine_prep_slave_sg(dma_dev->dmach,
				       &sg,
				       1,
				       DMA_MEM_TO_DEV,
				       DMA_INTERRUPT);

/*
	desc = dmaengine_prep_dma_memcpy(dmach,
					 dst,
					 dsrc,
					 count,
					 DMA_INTERRUPT);
*/
	if (IS_ERR(desc)) {
		dev_err(dev, "dmaengine_prep_slave_sg() failure\n");
		return -EIO;
	}

	/* CACHE SYNC HERE!!! */
	dma_sync_single_for_device(dma_dev->dmach->device->dev,
				   dsrc,
				   count,
				   DMA_TO_DEVICE);

	dma_drv_hack_chdir(desc);

	init_completion(&dma_dev->transfer_ok);
	desc->callback = dma_dev->dma_callback;
	desc->callback_param = &dma_dev->transfer_ok;
	cookie = dmaengine_submit(desc);

	if (dma_submit_error(cookie)) {
		dev_err(dev, "dma_submit_error() failure\n");
		return -EIO;
	}

	dev_info(dev, "Start waiting...\n");

	dma_async_issue_pending(dma_dev->dmach);
	wait_for_completion(&dma_dev->transfer_ok);

	return count;
}

/**********************/
/******** MMAP ********/
/**********************/
int dma_mmap(struct plng_dma_device * dma_dev,
		 struct file *filp,
		 struct vm_area_struct *vma)
{
	size_t pfn;
	size_t offset = vma->vm_pgoff << PAGE_SHIFT;
	size_t size = vma->vm_end - vma->vm_start;

	/* if (drv_vma) */
	/* return -EINVAL; */
	if (offset >= IOBUF_SIZE)
		return -EINVAL;
	if (size > (IOBUF_SIZE - offset))
		return -EINVAL;
	/* we can use page_to_pfn on the struct page structure
	 * returned by virt_to_page */
	/* pfn = page_to_pfn (virt_to_page (buffer + offset)); */
	/* Or make PAGE_SHIFT bits right-shift on the physical
	 * address returned by virt_to_phys */
	vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
	pfn = virt_to_phys(dma_dev->buf + offset) >> PAGE_SHIFT;
	if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot)) {
		dev_err(&dma_dev->pdev->dev, "remap_pfn_range() failure\n");
		return -EAGAIN;
	}
	dma_dev->usr_vma = vma;
	return 0;
}

/**********************/
/******** INIT ********/
/**********************/
int dma_init(struct plng_dma_device *dma_dev)
{
	int ret;
	dma_cap_mask_t mask;
	struct dma_chan *dmach;
	struct platform_device *pdev = dma_dev->pdev;
	struct device *dev = &pdev->dev;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dmach = dma_request_slave_channel(dev, "rxtx");

	if (IS_ERR(dmach)) {
		dev_err(dev, "dma_request_slave_channel() failure");
		return -ENODEV;
	}

	dma_dev->dmach = dmach;

	BUG_ON(!dma_dev->base);

	dma_dev->dma_base = dma_map_resource(dmach->device->dev,
					     (phys_addr_t)dma_dev->base,
					     dma_dev->base_size,
					     DMA_BIDIRECTIONAL, 0);
	
	if (dma_mapping_error(dmach->device->dev, dma_dev->dma_base)) {
		dev_err(dev, "dma_map_resource() fail");
		return -ENOMEM;
	}

	if (!dma_set_mask(dmach->device->dev, 0xffffff)) {
		dev_err(dev, "dma_set_mask() failure");
		ret = -ENODEV;
		goto IOREG_UNMAP;
	}

	BUG_ON(!dma_dev->buf);

        dma_dev->dma_buf = dma_map_single(dmach->device->dev, dma_dev->buf,
			       IOBUF_SIZE, DMA_BIDIRECTIONAL);

	if (dma_mapping_error(dmach->device->dev, dma_dev->dma_buf)) {
		dev_err(dev, "dma_map_single() fail");
		ret = -ENOMEM;
		goto IOREG_UNMAP;
	}

	return 0;

IOREG_UNMAP:
	dma_unmap_resource(dmach->device->dev,
			   dma_dev->dma_base,
			   dma_dev->base_size,
			   DMA_BIDIRECTIONAL, 0);
	return ret;
}

/**********************/
/******** EXIT ********/
/**********************/
void dma_fini(struct plng_dma_device *dma_dev)
{
	dmaengine_terminate_sync(dma_dev->dmach);	/* always success */

	dma_unmap_resource(dma_dev->dmach->device->dev,
			   dma_dev->dma_base,
			   dma_dev->base_size,
			   DMA_BIDIRECTIONAL, 0);
	dma_unmap_single(dma_dev->dmach->device->dev, dma_dev->dma_buf,
			 IOBUF_SIZE, DMA_BIDIRECTIONAL);
	return;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Doroshenko K");

