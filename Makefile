obj-$(CONFIG_PEL_DMA_DRV) := dma_driver.o

dma_driver-objs := dma_drv.o
dma_driver-objs += dma.o
dma_driver-objs += dma_pg.o
dma_driver-objs += iomemcpy.o

