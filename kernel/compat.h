#include <linux/version.h>
/* Simple compatibility macros */

#ifdef CONFIG_X86
   /* Readq for IA32 introduced in 2.6.28-rc7 */
   #ifndef readq

static inline __u64 readq(const volatile void __iomem *addr)
{
	const volatile u32 __iomem *p = addr;
	u32 low, high;

	low = readl(p);
	high = readl(p + 1);

	return low + ((u64)high << 32);
}

static inline void writeq(__u64 val, volatile void __iomem *addr)
{
	writel(val, addr);
	writel(val >> 32, addr+4);
}

   #endif
#endif /* X86 */

/*
 * request_firmware_nowait adds a gfp_t argument at some point:
 * patch 9ebfbd45f9d4ee9cd72529cf99e5f300eb398e67 == v2.6.32-5357-g9ebfbd4
 */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,32)
#define __RR_GFP_FOR_RFNW(x)  x,
#else
#define __RR_GFP_FOR_RFNW(x)  /* nothing */
#endif

/* Hack... something I sometimes need */
static inline void dumpstruct(char *name, void *ptr, int size)
{
	int i;
	unsigned char *p = ptr;

	printk("dump %s at %p (size 0x%x)\n", name, ptr, size);
	for (i = 0; i < size; ) {
		printk("%02x", p[i]);
		i++;
		printk(i & 3 ? " " : i & 0xf ? "  " : "\n");
	}
	if (i & 0xf)
		printk("\n");
}

