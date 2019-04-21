#if !defined(IOMEMCPY_H)
#define IOMEMCPY_H

int iomemcpy32_fromio(void *dst, const volatile void __iomem * src, size_t len);
int iomemcpy32_from_fifo(void *dst, const volatile void __iomem * src,
			 size_t len);
int iomemcpy64_fromio(void *dst, const volatile void __iomem * src, size_t len);

int iomemcpy32_toio(volatile void __iomem * dst, const void *src, size_t len);
int iomemcpy32_to_fifo(volatile void __iomem * dst, const void *src,
		       size_t len);
int iomemcpy64_toio(volatile void __iomem * dst, const void *src, size_t len);

#endif /* !defined(IOMEMCPY_H) */
