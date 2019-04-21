/**
 * @file:	fourier_pg.c
 * @version:	1.0.0
 * @date:	14 Dec 2016
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/errno.h>

#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/page-flags.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <asm/current.h>

#include "iomemcpy.h"
#include "dma_pg.h"
#include "khack.h"
#include "log.h"

#define MAX_PAGES_NUM		(64UL)

#define DMA_MAP_SG_DUMB /*DUMB!*/

#ifdef DMA_MAP_SG_DUMB
#define dma_drv_map_sg		dma_map_sg_dumb
#define dma_drv_unmap_sg	dma_unmap_sg_dumb
#else
#define dma_drv_map_sg		dma_map_sg_attrs
#define dma_drv_unmap_sg	dma_unmap_sg
#endif

#define DMA_DRV_DEV_BURST_LEN	(16)
#define DMA_DRV_MEM_BURST_LEN	(16)

#define DMA_DRV_READ_DIR 	DMA_DEV_TO_MEM
#define DMA_DRV_WRITE_DIR 	DMA_MEM_TO_DEV

#define DMA_DRV_READ_MAP_DIR 	DMA_FROM_DEVICE
#define DMA_DRV_WRITE_MAP_DIR 	DMA_TO_DEVICE

typedef struct {
	void __user *vaddr;
	void *kaddr;
	dma_addr_t daddr;
	size_t len;
	size_t off1st;
	size_t llast;
	size_t pgnum;
	size_t sgnum;
	struct page **pages;
	struct scatterlist *sgs;
	enum dma_data_direction dir;
} usrbuf_t;

static size_t
calc_pgs_num(usrbuf_t *usrbuf)
{
	size_t sz1st;
	if (usrbuf->len) {
		usrbuf->pgnum = 1;
	} else {
		return 0;
	}
	usrbuf->off1st = (size_t)(usrbuf->vaddr) & (PAGE_SIZE - 1);
	sz1st = PAGE_SIZE - usrbuf->off1st;
	if (sz1st > usrbuf->len) {
		usrbuf->llast = 0;
	} else {
		usrbuf->pgnum += (usrbuf->len - sz1st) / PAGE_SIZE;
		usrbuf->llast = (usrbuf->len - sz1st) & (PAGE_SIZE - 1);
		if (usrbuf->llast)
			++usrbuf->pgnum;
	}
	return usrbuf->pgnum;
}

static void
populate_sgs(usrbuf_t *usrbuf)
{
	size_t i, len = usrbuf->len;
	size_t sglen = min((size_t)(PAGE_SIZE - usrbuf->off1st), len);

	sg_init_table(usrbuf->sgs, usrbuf->pgnum);

	/* 1st page definitely has nonzero off */
	sg_set_page(usrbuf->sgs, usrbuf->pages[0], sglen, usrbuf->off1st);
	len -= sglen;
	/* iterate remaining sg */
	for (i = 1; i < usrbuf->pgnum; i++) {
		sglen = min((size_t)PAGE_SIZE, len);
		sg_set_page(usrbuf->sgs + i, usrbuf->pages[i], sglen, 0);
		len -= sglen;
	}
}

static int
dma_map_sg_dumb(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction dir,
		unsigned long attrs)
{
	int i;
	for (i = 0; i != nents; i++) {
		sg[i].dma_address = dma_map_page(dev,
						 (struct page *)sg[i].page_link,
						 sg[i].offset,
						 sg[i].length, dir);
		if (dma_mapping_error(dev, sg[i].dma_address)) {
			goto ROLL_BACK;
		}
	}
	return nents;
ROLL_BACK:
	printk(KERN_ERR
	       "dma_map_sg_dumb() fail, i=%d, &p=%px, o=%d, l=%d\n",
	       i, (struct page *)sg[i].page_link, sg[i].offset,
	       sg[i].length);
	if (--i > 0) {
		for (; i >= 0; i--) {
			dma_unmap_page(dev,
				       sg[i].dma_address, sg[i].length, dir);
		}
	}
	return 0;
}

static void
dma_unmap_sg_dumb(struct device *dev, struct scatterlist *sg,
		  int nents, enum dma_data_direction dir)
{
	int i;
	for (i = 0; i != nents; i++) {
		dma_unmap_page(dev, sg[i].dma_address, sg[i].length, dir);
	}
}

static usrbuf_t *get_usr_buf(struct plng_dma_device *dma_dev,
			     void __user * buf, size_t len,
			     enum dma_data_direction dir)
{
	size_t pgnum;
	size_t i;
	struct device *dev = &dma_dev->pdev->dev;
/* ALLOC_USR_BUF */
	usrbuf_t *usrbuf = kmalloc(sizeof(usrbuf_t), GFP_KERNEL);
	if (NULL == usrbuf) {
		printk(KERN_ERR "kmalloc() usrbuf_t error!\n");
		return NULL;
	}
	// calculate number of pages in user buf
	usrbuf->vaddr = buf;
	usrbuf->len = len;
	usrbuf->dir = dir;
	pgnum = calc_pgs_num(usrbuf);

/* ALLOC PAGES */
	usrbuf->pages = kmalloc(pgnum * sizeof(struct page *), GFP_KERNEL);
	if (NULL == usrbuf->pages) {
		dev_err(dev, "kmalloc() pages error!\n");
		goto FREE_USR_BUF;
	}

/* SEM DOWN */
	down_read(&current->mm->mmap_sem);

/* GET PAGES */
	pgnum = get_user_pages((unsigned long)buf,
				pgnum,
				FOLL_WRITE,
				usrbuf->pages,
				NULL);
	if (!pgnum) {
	        dev_err(dev, "get_user_pages() error!\n");
		goto SEM_UP;
	}

	usrbuf->pgnum = pgnum;

/* ALLOC SGS */
	usrbuf->sgs = kmalloc(pgnum * sizeof(struct scatterlist), GFP_KERNEL);
	if (NULL == usrbuf->sgs) {
	        dev_err(dev, "kmalloc() sgs error!\n");
		goto PUT_PAGES;
	}

	populate_sgs(usrbuf);

/* DMA MAP SG */
	usrbuf->sgnum = dma_drv_map_sg(dma_dev->dmach->device->dev,
				       usrbuf->sgs,
				       usrbuf->pgnum,
				       usrbuf->dir,
				       0);

	if (usrbuf->sgnum == 0) {
	        dev_err(dev, "dma_map_sg() error!\n");
		goto FREE_SGS;
	}
	up_read(&current->mm->mmap_sem);
	return usrbuf;

FREE_SGS:			/* !ALLOC SGS */
	kfree(usrbuf->sgs);
PUT_PAGES:			/* !GET PAGES */
	for (i = 0; i < usrbuf->pgnum; ++i)
		put_page(usrbuf->pages[i]);
SEM_UP:			/* !SEM DOWN */
	up_read(&current->mm->mmap_sem);
/* FREE_PAGES:				!ALLOC PAGES */
	kfree(usrbuf->pages);
FREE_USR_BUF:			/* !ALLOC_USR_BUF */
	kfree(usrbuf);
	return 0;
}

static void put_usr_buf(struct plng_dma_device *dma_dev, usrbuf_t * usrbuf)
{
	size_t i;
/* UNMAP_SG:					 !DMA MAP SG */
	dma_drv_unmap_sg(dma_dev->dmach->device->dev,
			 usrbuf->sgs,
			 usrbuf->sgnum,
			 usrbuf->dir);
/* FREE_SGS:					 !ALLOC SGS */
	kfree(usrbuf->sgs);
/* PUT_PAGES:				   !GET PAGES */
	for (i = 0; i < usrbuf->pgnum; ++i)
		put_page(usrbuf->pages[i]);
/* FREE_PAGES:				!ALLOC PAGES */
	kfree(usrbuf->pages);
/* FREE_USR_BUF:			!ALLOC_USR_BUF */
	kfree(usrbuf);
}

void print_pg(const void *pg_data)
{
	print_hex_dump("", "", DUMP_PREFIX_OFFSET,
		       32, 1, pg_data, PAGE_SIZE, false);
}

void print_sg(usrbuf_t * usrbuf)
{
	size_t i;
	for (i = 0; i != usrbuf->sgnum; i++) {
		struct scatterlist *sg = &usrbuf->sgs[i];
		dma_addr_t daddr = sg_dma_address(sg);
		unsigned int len = sg_dma_len(sg);
		printk(KERN_INFO "daddr = %px, len = %u, offset = %u\n",
		       (void *)daddr, len, sg->offset);
	}
}

/**********************/
/******** READ ********/
/**********************/
ssize_t dma_read_pg(struct plng_dma_device *dma_dev,
		    void __user * dst,
		    const loff_t br_offset,
		    size_t count)
{
	ssize_t ret = -1;
	struct device *dev = &dma_dev->pdev->dev;
	struct dma_async_tx_descriptor *desc;
	struct dma_slave_config conf;
	dma_cookie_t cookie;
	usrbuf_t *usrbuf;

/* GET USR BUF */
	usrbuf = get_usr_buf(dma_dev,
			     (void __user *)dst,
			     count,
			     DMA_DRV_READ_MAP_DIR);
	if (!usrbuf) {
		dev_err(dev, "get_usr_buf() error!");
		return -ENOENT;
	}

	dev_info(dev, "Num sg entries = %u", usrbuf->sgnum);
	/* print_sg(usrbuf); */

	memzero_explicit(&conf, sizeof(struct dma_slave_config));
	conf.direction = DMA_DRV_READ_DIR;
	conf.src_addr = (phys_addr_t)(dma_dev->dma_base + br_offset);
	conf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	conf.src_maxburst = DMA_DRV_DEV_BURST_LEN;
	conf.dst_maxburst = DMA_DRV_MEM_BURST_LEN;

	if (dmaengine_slave_config(dma_dev->dmach, &conf) < 0) {
		dev_err(dev, "dmaengine_slave_config() failure");
		ret = -EIO;
		goto PUT_USR_BUF;
	}

	desc = dmaengine_prep_slave_sg(dma_dev->dmach,
				       usrbuf->sgs,
				       usrbuf->sgnum,
				       DMA_DRV_READ_DIR, DMA_INTERRUPT);

/*
  desc = dmaengine_prep_dma_memcpy(dmach,
                                   usrbuf->sgs[0].dma_address,
                                   src,
                                   usrbuf->sgs[0].length,
                                   DMA_INTERRUPT);
*/
	if (IS_ERR(desc)) {
		dev_err(dev, "dmaengine_prep_slave_sg() failure\n");
		ret = -EIO;
		goto PUT_USR_BUF;
	}

	dma_drv_hack_chdir(desc);

	init_completion(&dma_dev->transfer_ok);
	desc->callback = dma_dev->dma_callback;
	desc->callback_param = &dma_dev->transfer_ok;
	cookie = dmaengine_submit(desc);

	if (0 != dma_submit_error(cookie)) {
		dev_err(dev, "dma_submit() failure\n");
		ret = -EIO;
		goto PUT_USR_BUF;
	}

	dma_async_issue_pending(dma_dev->dmach);
	dev_info(dev, "Start waiting...\n");

	wait_for_completion(&dma_dev->transfer_ok);

	ret = count;

PUT_USR_BUF:			/* !GET USR BUF */
	put_usr_buf(dma_dev, usrbuf);

	return (ret);
}

/**********************/
/******* WRITE ********/
/**********************/
ssize_t dma_write_pg(struct plng_dma_device * dma_dev,
		     const void __user * src,
		     loff_t br_offset,
		     size_t count)
{
	ssize_t ret = -1;
	struct dma_async_tx_descriptor *desc;
	struct dma_slave_config conf;
	struct device *dev = &dma_dev->pdev->dev;
	dma_cookie_t cookie;
	usrbuf_t *usrbuf;

/* GET USR BUF */
	usrbuf = get_usr_buf(dma_dev,
			     (void __user *)src,
			     count,
			     DMA_DRV_WRITE_MAP_DIR);
	if (!usrbuf) {
		dev_err(dev, "get_usr_buf() error!\n");
		return -ENOENT;
	}

	dev_info(dev, "Num sg entries = %u\n", usrbuf->sgnum);
	/* print_sg(usrbuf); */

	memzero_explicit(&conf, sizeof(struct dma_slave_config));
	conf.direction = DMA_DRV_WRITE_DIR;
	conf.dst_addr = (phys_addr_t)(dma_dev->dma_base + br_offset);
	conf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	conf.src_maxburst = DMA_DRV_MEM_BURST_LEN;
	conf.dst_maxburst = DMA_DRV_DEV_BURST_LEN;

	if (dmaengine_slave_config(dma_dev->dmach, &conf) < 0) {
		dev_err(dev, "dmaengine_slave_config() failure\n");
		ret = -EIO;
		goto PUT_USR_BUF;
	}

	desc = dmaengine_prep_slave_sg(dma_dev->dmach,
				       usrbuf->sgs,
				       usrbuf->sgnum,
				       DMA_DRV_WRITE_DIR, DMA_INTERRUPT);

/*
	desc = dmaengine_prep_dma_memcpy(dmach,
					 dst,
					 usrbuf->sgs[0].dma_address + \
					 usrbuf->sgs[0].offset,
					 usrbuf->sgs[0].length,
					 DMA_INTERRUPT);
*/
	if (IS_ERR(desc)) {
		dev_err(dev, "dmaengine_prep_slave_sg() failure\n");
		ret = -EIO;
		goto PUT_USR_BUF;
	}

	dma_drv_hack_chdir(desc);

	init_completion(&dma_dev->transfer_ok);
	desc->callback = dma_dev->dma_callback;
	cookie = dmaengine_submit(desc);

	if (0 != dma_submit_error(cookie)) {
		dev_err(dev, "dma_submit_error() failure\n");
		ret = -EIO;
		goto PUT_USR_BUF;
	}

	dma_async_issue_pending(dma_dev->dmach);
	dev_info(dev, "Start waiting...\n");
	wait_for_completion(&dma_dev->transfer_ok);

	ret = count;

PUT_USR_BUF:			/* !GET USR BUF */
	put_usr_buf(dma_dev, usrbuf);

	return (ret);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Doroshenko K");
