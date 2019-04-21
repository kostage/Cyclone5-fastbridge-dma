#ifndef _KERNEL_HACK_ATTEMPT_
#define _KERNEL_HACK_ATTEMPT_

#include <linux/dmaengine.h>
#include <linux/types.h>
#include <linux/interrupt.h>

#include "log.h"

enum desc_status {
	/* In the DMAC pool */
	FREE,
	/*
	 * Allocated to some channel during prep_xxx
	 * Also may be sitting on the work_list.
	 */
	PREP,
	/*
	 * Sitting on the work_list and already submitted
	 * to the PL330 core. Not more than two descriptors
	 * of a channel can be BUSY at any time.
	 */
	BUSY,
	/*
	 * Sitting on the channel work_list but xfer done
	 * by PL330 core
	 */
	DONE,
};

enum pl330_byteswap {
	SWAP_NO,
	SWAP_2,
	SWAP_4,
	SWAP_8,
	SWAP_16,
};

enum pl330_cachectrl {
	CCTRL0,			/* Noncacheable and nonbufferable */
	CCTRL1,			/* Bufferable only */
	CCTRL2,			/* Cacheable, but do not allocate */
	CCTRL3,			/* Cacheable and bufferable, but do not allocate */
	INVALID1,		/* AWCACHE = 0x1000 */
	INVALID2,
	CCTRL6,			/* Cacheable write-through, allocate on writes only */
	CCTRL7,			/* Cacheable write-back, allocate on writes only */
};

/* Populated by the PL330 core driver for DMA API driver's info */
struct pl330_config {
	u32 periph_id;
#define DMAC_MODE_NS	(1 << 0)
	unsigned int mode;
	unsigned int data_bus_width:10;	/* In number of bits */
	unsigned int data_buf_dep:11;
	unsigned int num_chan:4;
	unsigned int num_peri:6;
	u32 peri_ns;
	unsigned int num_events:6;
	u32 irq_ns;
};

/**
 * Request Configuration.
 * The PL330 core does not modify this and uses the last
 * working configuration if the request doesn't provide any.
 *
 * The Client may want to provide this info only for the
 * first request and a request with new settings.
 */
struct pl330_reqcfg {
	/* Address Incrementing */
	unsigned dst_inc:1;
	unsigned src_inc:1;

	/*
	 * For now, the SRC & DST protection levels
	 * and burst size/length are assumed same.
	 */
	bool nonsecure;
	bool privileged;
	bool insnaccess;
	unsigned brst_len:5;
	unsigned brst_size:3;	/* in power of 2 */

	enum pl330_cachectrl dcctl;
	enum pl330_cachectrl scctl;
	enum pl330_byteswap swap;
	struct pl330_config *pcfg;
};

/*
 * One cycle of DMAC operation.
 * There may be more than one xfer in a request.
 */
struct pl330_xfer {
	u32 src_addr;
	u32 dst_addr;
	/* Size to xfer */
	u32 bytes;
};

struct dma_pl330_chan {
	/* Schedule desc completion */
	struct tasklet_struct task;

	/* DMA-Engine Channel */
	struct dma_chan chan;

	/* List of submitted descriptors */
	struct list_head submitted_list;
	/* List of issued descriptors */
	struct list_head work_list;
	/* List of completed descriptors */
	struct list_head completed_list;

	/* Pointer to the DMAC that manages this channel,
	 * NULL if the channel is available to be acquired.
	 * As the parent, this DMAC also provides descriptors
	 * to the channel.
	 */
	struct pl330_dmac *dmac;

	/* To protect channel manipulation */
	spinlock_t lock;

	/*
	 * Hardware channel thread of PL330 DMAC. NULL if the channel is
	 * available.
	 */
	struct pl330_thread *thread;

	/* For D-to-M and M-to-D channels */
	int burst_sz;		/* the peripheral fifo width */
	int burst_len;		/* the number of burst */
	phys_addr_t fifo_addr;
	/* DMA-mapped view of the FIFO; may differ if an IOMMU is present */
	dma_addr_t fifo_dma;
	enum dma_data_direction dir;

	/* for cyclic capability */
	bool cyclic;

	/* for runtime pm tracking */
	bool active;
};

struct dma_pl330_desc {
	/* To attach to a queue as child */
	struct list_head node;

	/* Descriptor for the DMA Engine API */
	struct dma_async_tx_descriptor txd;

	/* Xfer for PL330 core */
	struct pl330_xfer px;

	struct pl330_reqcfg rqcfg;

	enum desc_status status;

	int bytes_requested;
	bool last;

	/* The channel which currently holds this desc */
	struct dma_pl330_chan *pchan;

	enum dma_transfer_direction rqtype;
	/* Index of peripheral for the xfer. */
	unsigned peri:5;
	/* Hook to attach to DMAC's list of reqs with due callback */
	struct list_head rqd;
};

static inline struct dma_pl330_desc *to_desc(struct dma_async_tx_descriptor *tx)
{
	return container_of(tx, struct dma_pl330_desc, txd);
}

static inline
struct dma_pl330_chan *to_pchan(struct dma_chan *ch)
{
	if (!ch)
		return NULL;

	return container_of(ch, struct dma_pl330_chan, chan);
}

static inline void
dma_drv_hack_chdir(struct dma_async_tx_descriptor *tx)
{
	struct dma_pl330_desc *desc, *last = to_desc(tx);
	list_for_each_entry(desc, &last->node, node) {
		LOG_INFO("chdir: %d -> %d\n", desc->rqtype, DMA_MEM_TO_MEM);
		desc->rqtype = DMA_MEM_TO_MEM;
	}
	LOG_INFO("chdir: %d -> %d\n", last->rqtype, DMA_MEM_TO_MEM);
	last->rqtype = DMA_MEM_TO_MEM;
}

static inline void
dma_drv_hack_setfifo(struct dma_async_tx_descriptor *tx,
		     enum dma_transfer_direction dir)
{
	struct dma_pl330_desc *desc, *last = to_desc(tx);
	if (dir == DMA_DEV_TO_MEM) {
		list_for_each_entry(desc, &last->node, node) {
			desc->rqcfg.src_inc = 0;
		}
		desc->rqcfg.src_inc = 0;
	} else if (dir == DMA_MEM_TO_DEV) {
		list_for_each_entry(desc, &last->node, node) {
			desc->rqcfg.dst_inc = 0;
		}
		desc->rqcfg.dst_inc = 0;
	}
}

static inline void
dma_drv_hack_mkcyclic(struct dma_chan *chan, int cyclic)
{
	struct dma_pl330_chan *pch = to_pchan(chan);
	pch->cyclic = cyclic;
}

#endif /* _KERNEL_HACK_ATTEMPT_ */
