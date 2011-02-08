/*
 * This is the low-level engine of firmware loading. It is meant
 * to be compiled both as kernel code and user code, using the associated
 * header to differentiate
 */

#define __LOADER_LL_C__ /* Callers won't define this symbol */

#include "rawrabbit.h"
#include "loader-ll.h"

static inline uint8_t reverse_bits8(uint8_t x)
{
	x = ((x >> 1) & 0x55) | ((x & 0x55) << 1);
	x = ((x >> 2) & 0x33) | ((x & 0x33) << 2);
	x = ((x >> 4) & 0x0f) | ((x & 0x0f) << 4);

	return x;
}

static uint32_t unaligned_bitswap_le32(const uint32_t *ptr32)
{
	static uint32_t tmp32;
	static uint8_t *tmp8 = (uint8_t *) &tmp32;
	static uint8_t *ptr8;

	ptr8 = (uint8_t *) ptr32;

	*(tmp8 + 0) = reverse_bits8(*(ptr8 + 0));
	*(tmp8 + 1) = reverse_bits8(*(ptr8 + 1));
	*(tmp8 + 2) = reverse_bits8(*(ptr8 + 2));
	*(tmp8 + 3) = reverse_bits8(*(ptr8 + 3));

	return tmp32;
}

/*
 * Unfortunately, most of the following is from fcl_gn4124.cpp, for which
 * the license terms are at best ambiguous. 
 */

int loader_low_level(int fd, void __iomem *bar4, const void *data, int size8)
{
	int size32 = (size8 + 3) >> 2;
	const uint32_t *data32 = data;
	int ctrl = 0, i, done = 0, wrote = 0;

	lll_write(fd, bar4, 0x00, FCL_CLK_DIV);
	lll_write(fd, bar4, 0x40, FCL_CTRL); /* Reset */
	i = lll_read(fd, bar4, FCL_CTRL);
	if (i != 0x40) {
		printk(KERN_ERR "%s: %i: error\n", __func__, __LINE__);
		return -EIO;
	}
	lll_write(fd, bar4, 0x00, FCL_CTRL);

	lll_write(fd, bar4, 0x00, FCL_IRQ); /* clear pending irq */

	switch(size8 & 3) {
	case 3: ctrl = 0x116; break;
	case 2: ctrl = 0x126; break;
	case 1: ctrl = 0x136; break;
	case 0: ctrl = 0x106; break;
	}
	lll_write(fd, bar4, ctrl, FCL_CTRL);

	lll_write(fd, bar4, 0x00, FCL_CLK_DIV); /* again? maybe 1 or 2? */

	lll_write(fd, bar4, 0x00, FCL_TIMER_CTRL); /* "disable FCL timr fun" */

	lll_write(fd, bar4, 0x10, FCL_TIMER_0); /* "pulse width" */
	lll_write(fd, bar4, 0x00, FCL_TIMER_1);

	/*
	 * Set delay before data and clock is applied by FCL
	 * after SPRI_STATUS is	detected being assert.
	 */
	lll_write(fd, bar4, 0x08, FCL_TIMER2_0); /* "delay before data/clk" */
	lll_write(fd, bar4, 0x00, FCL_TIMER2_1);
	lll_write(fd, bar4, 0x17, FCL_EN); /* "output enable" */

	ctrl |= 0x01; /* "start FSM configuration" */
	lll_write(fd, bar4, ctrl, FCL_CTRL);

	while(size32 > 0)
	{
		/* Check to see if FPGA configuation has error */
		i = lll_read(fd, bar4, FCL_IRQ);
		if ( (i & 8) && wrote) {
			done = 1;
			printk("%s: %i: done after %i\n", __func__, __LINE__,
				wrote);
		} else if ( (i & 0x4) && !done) {
			printk("%s: %i: error after %i\n", __func__, __LINE__,
				wrote);
			return -EIO;
		}

		/* Wait until at least 1/2 of the fifo is empty */
		while (lll_read(fd, bar4, FCL_IRQ)  & (1<<5))
			;

		/* Write a few dwords into FIFO at a time. */
		for (i = 0; size32 && i < 32; i++) {
			lll_write(fd, bar4, unaligned_bitswap_le32(data32),
				  FCL_FIFO);
			data32++; size32--; wrote++;
		}
	}

	lll_write(fd, bar4, 0x186, FCL_CTRL); /* "last data written" */

	/* Checking for the "interrupt" condition is left to the caller */
	return wrote;
}
