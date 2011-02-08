/*
 * This header differentiates between kernel-mode and user-mode compilation,
 * as loader-ll.c is meant to be used in both contexts.
 */

#ifndef __iomem
#define __iomem /* nothing, for user space */
#endif

extern int loader_low_level(
	int fd,			/* This is ignored in kernel space */
	void __iomem *bar4,	/* This is ignored in user space */
	const void *data,
	int size8);


/* The following part implements a different access rule for user and kernel */
#ifdef __LOADER_LL_C__

#ifdef __KERNEL__

#include <asm/io.h>
//#include <linux/kernel.h> /* for printk */

static inline void lll_write(int fd, void __iomem *bar4, u32 val, int reg)
{
	writel(val, bar4 + reg);
}

static inline u32 lll_read(int fd, void __iomem *bar4, int reg)
{
	return readl(bar4 + reg);
}

#else /* ! __KERNEL__ */

/* FIXME: User space macros */

#endif

#endif /* __LOADER_LL_C__ */
