#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/const.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>

#include <linux/gfp.h>
#include <linux/uaccess.h>
#include <linux/ioport.h>

#include <linux/bitops.h>
#include <linux/semaphore.h>
#include <linux/hrtimer.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>

#include "plng_dma_device.h"
#include "rlsctl.h"
#include "dma.h"
#include "dma_pg.h"
#include "iomemcpy.h"
#include "log.h"


typedef ssize_t (*wr_func_t)(struct plng_dma_device *dma_dev,
			     const void __user *src,
			     loff_t br_offset,
			     size_t count);

typedef ssize_t (*rd_func_t)(struct plng_dma_device *dma_dev,
			     void __user * dst,
			     const loff_t br_offset,
			     size_t count);

struct rd_op {
	rd_func_t rdfunc;
	char *name;
};

struct wr_op {
	wr_func_t wrfunc;
	char *name;
};

struct op {
	struct rd_op rdop;
	struct wr_op wrop;
};

static inline
struct plng_dma_device *file_to_dma_dev(struct file *file)
{
	struct miscdevice *misc = file->private_data;
	return container_of(misc, struct plng_dma_device, mdev);
}

static void dma_callback(void *completion)
{
	pr_info("Hello from callback\n");
	complete(completion);
	return;
}

ssize_t dumb_read(struct plng_dma_device *dma_dev,
		  void __user * dst,
		  const loff_t br_offset,
		  size_t len)
{
	if (dma_dev->fifo_mode == FIFO_ADDR) {
		iomemcpy32_from_fifo(dma_dev->buf,
				     dma_dev->base + br_offset,
				     len);
	} else if (dma_dev->fifo_mode == INCR_ADDR) {
		memcpy_fromio(dma_dev->buf,
			      dma_dev->base + br_offset,
			      len);
	}

	if (0L != copy_to_user(dst, dma_dev->buf, len))
		return (-EFAULT);
	return (ssize_t)len;
}

ssize_t dumb_write(struct plng_dma_device *dma_dev,
		   const void __user *src,
		   loff_t br_offset,
		   size_t len)
{
	if (0L != copy_from_user(dma_dev->buf,
				 src,
				 len))
		return (-EFAULT);

	if (dma_dev->fifo_mode == FIFO_ADDR) {
		iomemcpy32_to_fifo(dma_dev->base + br_offset,
				   src,
				   len);
	} else if (dma_dev->fifo_mode == INCR_ADDR) {
		memcpy_toio(dma_dev->base + br_offset,
			    src,
			    len);
	}
	return len;
}

static int offset_error(loff_t off, size_t len)
{
	if (off) {
		printk(KERN_ERR "Offset not supported!\n");
		return 1;
	}
/*
	if (regions_tab[bridge].size < len) {
		printk(KERN_ERR "Len greater than ioreg!\n");
		return 1;
	}
*/
	return 0;
}

static struct op ops[] = {
	{{&dumb_read, "DUMB_READ"}, {&dumb_write, "DUMB_WRITE"}},
	{{&dma_read, "DMA_READ"}, {&dma_write, "DMA_WRITE"}},
	{{&dma_read_pg, "DMAPG_READ"}, {&dma_write_pg, "DMAPG_WRITE"}}
};

/**********************/
/******** READ ********/
/**********************/
static ssize_t
dma_drv_read(struct file *fp, char __user *dst, size_t len,
	     loff_t *off)
{
	struct plng_dma_device * dma_dev = file_to_dma_dev(fp);
	struct device *dev = &dma_dev->pdev->dev;
	struct rd_op *rdop = &ops[dma_dev->dma_mode].rdop;
	dev_info(dev, "op: %s, len = %d\n", rdop->name, len);

	if (dma_dev->fifo_mode != FIFO_ADDR && offset_error(*off, len))
		return (-EINVAL);

	return rdop->rdfunc(dma_dev, dst, *off, len);
}

/**********************/
/******* WRITE ********/
/**********************/
static ssize_t
dma_drv_write(struct file *fp, const char __user *src, size_t len,
	      loff_t *off)
{
	struct plng_dma_device * dma_dev = file_to_dma_dev(fp);
	struct device *dev = &dma_dev->pdev->dev;
	struct wr_op *wrop = &ops[dma_dev->dma_mode].wrop;
	dev_info(dev, "op: %s, len = %d\n", wrop->name, len);

	if (dma_dev->fifo_mode != FIFO_ADDR && offset_error(*off, len))
		return (-EINVAL);

	return wrop->wrfunc(dma_dev, src, *off, len);
}

/**********************/
/******* IOCTL ********/
/**********************/
static long
dma_drv_ioctl(struct file *filp, unsigned int cmd,
	      unsigned long arg)
{
	long retval = 0L;
	unsigned dir = _IOC_DIR(cmd);

	struct plng_dma_device * dma_dev = file_to_dma_dev(filp);

	switch (_IOC_NR(cmd)) {
	case OPMODE:
		if (dir == _IOC_READ)
			retval = dma_dev->dma_mode;
		else
		        dma_dev->dma_mode = arg;
		
		break;
	case INCRADDR:
		if (dir == _IOC_READ)
			retval = dma_dev->fifo_mode;
		else
		        dma_dev->fifo_mode = arg;
		break;
	default:
		return (-ENOTTY);
	}
	return retval;
}

/**********************/
/******** MMAP ********/
/**********************/
int dma_drv_mmap (struct file *filp, struct vm_area_struct *vma)
{
	struct plng_dma_device * dma_dev = file_to_dma_dev(filp);
	return  dma_mmap(dma_dev, filp, vma);
}

static struct file_operations dma_drv_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.read = dma_drv_read,
	.write = dma_drv_write,
	.unlocked_ioctl = dma_drv_ioctl,
	.mmap = dma_drv_mmap,
	.open = nonseekable_open,
};

/**********************/
/******** INIT ********/
/**********************/
static int dma_drv_anal_probe(struct platform_device *pdev)
{
	int ret;

	struct plng_dma_device *dma_dev;
	struct device *dev = &pdev->dev;
	struct resource *res;

	dev_info(dev, "HELLO\n");
	dma_dev = devm_kzalloc(dev, sizeof(*dma_dev), GFP_KERNEL);
	if (IS_ERR(dma_dev)) {
		dev_err(dev, "devm_kzalloc fail");
		return -ENOMEM;
	}

#ifdef CONFIG_DMA_ENGINE
	dev_info(dev, "DMA ENGINE ON");
#else
	dev_info(dev, "DMA ENGINE OFF");
#endif

	dma_dev->buf = (void*)devm_get_free_pages(dev, GFP_KERNEL, get_order(IOBUF_SIZE));
	if (IS_ERR(dma_dev->buf)) {
		dev_err(dev, "get_free_pages fail");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (IS_ERR(res)) {
		dev_err(dev, "platform_get_resource fail");
		return -ENOMEM;
	}

	if (!devm_request_mem_region(dev,
				     res->start,
				     resource_size(res),
				     pdev->name)) {
		dev_err(dev, "devm_request_mem_region fail");
		return -EBUSY;
	}

	dma_dev->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(dma_dev->base)) {
		dev_err(dev, "devm_request_mem_region fail");
		return -ENOMEM;
	}
	dma_dev->base_size = resource_size(res);

	dma_dev->dma_mode = DUMB_OPMODE;
	dma_dev->fifo_mode = FIFO_ADDR;
	dma_dev->pdev = pdev;
	dma_dev->dma_callback = &dma_callback;

	/* must be done before dma_init */
	platform_set_drvdata(pdev, dma_dev);

	if ((ret = dma_init(dma_dev)) != 0) {
		dev_err(dev, "dma_init fail");
		return ret;
	}

	dma_dev->mdev.minor  = MISC_DYNAMIC_MINOR;
	dma_dev->mdev.name   = "dma_miscdev";
	dma_dev->mdev.fops   = &dma_drv_fops;
	dma_dev->mdev.parent = dev;

	ret = misc_register(&dma_dev->mdev);
	if (ret) {
		dev_err(dev, "Failed to register miscdev\n");
		goto DMA_FINI;
	}
	dev_info(dev, "misc_register 10:%d done\n", dma_dev->mdev.minor);

	return 0;

DMA_FINI:
	dma_fini(dma_dev);
	return ret;
}

/**********************/
/******** EXIT ********/
/**********************/
static int dma_drv_anal_remove(struct platform_device *pdev)
{
	struct plng_dma_device *dma_dev = platform_get_drvdata(pdev);
	misc_deregister(&dma_dev->mdev);
	dma_fini(dma_dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id dma_drv_of_match[] = {
	{ .compatible = "plng,dma_driver", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dma_drv_of_match);
#endif

static struct platform_driver dma_drv_driver = {
	.probe		= dma_drv_anal_probe,
	.remove		= dma_drv_anal_remove,
	.driver		= {
		.name	= "dma_driver",
		.of_match_table = of_match_ptr(dma_drv_of_match),
	},
};

module_platform_driver(dma_drv_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Doroshenko K");
