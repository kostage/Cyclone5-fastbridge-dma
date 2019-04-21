#ifndef __DMA_DRV_LOG_H__
#define __DMA_DRV_LOG_H__

//#define DEBUG

#ifdef DEBUG
#define LOG_ERR(x...)           printk(KERN_ERR x)
#define LOG_INFO(x...)          printk(KERN_INFO x)
#else
#define LOG_ERR(x...)
#define LOG_INFO(x...)
#endif

#endif /* __DMA_DRV_LOG_H__ */
