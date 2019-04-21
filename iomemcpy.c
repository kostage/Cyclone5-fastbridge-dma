#include <linux/module.h>

#include <linux/types.h>
#include <asm/io.h>

#include "iomemcpy.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Doroshenko K");

#define	fail_on_inval_args(addr1, addr2, len, align)			\
do {									\
	if (0UL != ((unsigned long)(addr1) & ((align) - 1U))		\
	    || 0UL != ((unsigned long)(addr2) & ((align) - 1U))		\
	    || 0U != ((len) & ((align) - 1U)))				\
		return (0);						\
} while (0)

int iomemcpy32_fromio(void *dst, const volatile void __iomem * src, size_t len)
{
	fail_on_inval_args(dst, src, len, 4U);
	for (; 0U != len; len -= 4U) {
		*((u32 *) dst) = __raw_readl(src);
		dst = (void *)((u8 *) dst + 4U);
		src = (const volatile void __iomem *)((u8 *) src + 4U);
	}
	return (1);
}

int iomemcpy32_from_fifo(void *dst, const volatile void __iomem * src,
			 size_t len)
{
	fail_on_inval_args(dst, src, len, 4U);
	for (; 0U != len; len -= 4U) {
		*((u32 *) dst) = __raw_readl(src);
		dst = (void *)((u8 *) dst + 4U);
	}
	return (1);
}

int iomemcpy64_fromio(void *dst, const volatile void __iomem * src, size_t len)
{
	fail_on_inval_args(dst, src, len, 8U);
	for (; 0U != len; len -= 8U) {
		asm volatile ("ldrd %1, %0, #8":"+Qo"
			      (*((volatile u32 __force *)src)),
			      "=r"(*((u64 *) dst)));
		dst = (void *)((u8 *) dst + 8U);
	}
	return (1);
}

int iomemcpy32_toio(volatile void __iomem * dst, const void *src, size_t len)
{
	fail_on_inval_args(dst, src, len, 4U);
	for (; 0U != len; len -= 4U) {
		__raw_writel(*((u32 *) src), dst);
		dst = (volatile void __iomem *)((u8 *) dst + 4U);
		src = (const void *)((u8 *) src + 4U);
	}
	return (1);
}

int iomemcpy32_to_fifo(volatile void __iomem * dst, const void *src, size_t len)
{
	fail_on_inval_args(dst, src, len, 4U);
	for (; 0U != len; len -= 4U) {
		__raw_writel(*((u32 *) src), dst);
		src = (const void *)((u8 *) src + 4U);
	}
	return (1);
}

int iomemcpy64_toio(volatile void __iomem * dst, const void *src, size_t len)
{
	fail_on_inval_args(dst, src, len, 8U);
	for (; 0U != len; len -= 8U) {
		asm volatile ("strd %1, %0, #8":"+Qo"
			      (*((volatile u32 __force *)dst))
			      :"r"(*((u64 *) src)));
		src = (const void *)((u8 *) src + 8U);
	}
	return (1);
}
