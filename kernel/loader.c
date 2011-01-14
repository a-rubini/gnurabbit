
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/hardirq.h>
#include <linux/workqueue.h>
#include <linux/firmware.h>
#include <linux/jiffies.h>

#include "rawrabbit.h"
#include "compat.h"

#ifndef CONFIG_FW_LOADER
#warning "CONFIG_FW_LOADER is need in kernel configuration."
#warning "Compiling anyways, but won't be able to load firmware"
#endif

static void __rr_gennum_load(struct rr_dev *dev, const void *data, int size);

/* This stupid function is used to report what is the calling environment */
static void __rr_report_env(const char *func)
{
	/* Choose 0 or 1 in the if below, to turn on/off without warnings */
	if (1) {
		printk("%s: called with preempt_cont == 0x%08x\n", func,
		       preempt_count());
		printk("%s: current = %i: %s\n", func,
		       current->pid, current->comm);
	}
}

/*
 * The callback when loading is over, either with or without data.
 * This a context that can sleep, so we can program it taking as
 * much time as we want.
 */
static void rr_loader_complete(const struct firmware *fw, void *context)
{
	struct rr_dev *dev = context;

	dev->fw = fw;

	__rr_report_env(__func__); /* Here, preempt count is 0: good! */

	if (fw) {
		pr_info("%s: %p: size %i (0x%x), data %p\n", __func__, fw,
			fw ? fw->size : 0, fw ? fw->size : 0,
			fw ? fw->data : 0);
	} else {
		pr_warning("%s: no firmware\n", __func__);
		return;
	}

	/*
	 * At this point, use fw->data as a linear array of fw->size bytes.
	 * If we are handling the gennum device, load this firmware binary;
	 * otherwise, just complain to the user -- I'd better use a much
	 * fancier method of defining one programmer per vendor/device...
	 */
	if (dev->devsel->vendor == RR_DEFAULT_VENDOR
	    && dev->devsel->device == RR_DEFAULT_DEVICE) {
		__rr_gennum_load(dev, fw->data, fw->size);
	} else {
		pr_warning("%s: not loading firmware: this is not a GN4124\n",
			   __func__);
	}
	/* At this point, we can releae the firmware we got */
	release_firmware(dev->fw);
	dev->fw = NULL;
}

/*
 * We want to run the actual loading from a work queue, to have a known
 * loading environment, especially one that can sleep. The function
 * pointer is already in the work structure, set at compile time from
 * rawrabbit-core.c
 */
void rr_load_firmware(struct work_struct *work)
{
	struct rr_dev *dev = container_of(work, struct rr_dev, work);
	struct rr_devsel *devsel = dev->devsel;
	struct pci_dev *pdev = dev->pdev;
	static char fwname[64];
	int err;

	/* Create firmware name */
	sprintf(fwname, "rrabbit-%04x:%04x-%04x:%04x@%04x:%04x",
		       devsel->vendor, devsel->device,
		       devsel->subvendor, devsel->subdevice,
		       devsel->bus, devsel->devfn);
	if (1)
		printk("%s: %s\n", __func__, fwname);

	__rr_report_env(__func__);
	err = request_firmware_nowait(THIS_MODULE, 1, fwname, &pdev->dev,
				      __RR_GFP_FOR_RFNW(GFP_KERNEL)
				      dev, rr_loader_complete);
	printk("request firmware returned %i\n", err);

}

/* This function is called by the PCI probe function. */
void rr_ask_firmware(struct rr_dev *dev)
{
	/* Here, preempt_cont is 1 for insmode/rrcmd. What for hotplug? */
	__rr_report_env(__func__);

	if (dev->fw) {
		pr_err("%s: firmware asked, but firmware already present\n",
			__func__);
		return;
	}

	/* Activate the work queue, so we are sure we are in user context */
	schedule_work(&dev->work);
}

/*
 * Finally, this is the most important thing, the loader for a Gennum
 * 4124 evaluation board. We know we are mounting a Xilinx Spartan.
 *
 * Unfortunately, most of this is from fcl_gn4124.cpp, for which the
 * license terms are at best ambiguous. 
 */
static void __rr_gennum_load(struct rr_dev *dev, const void *data, int size8)
{
	int i, ctrl, done = 0, wrote = 0;
	unsigned long j;
	u8 val8;
	const u8 *data8 = data;
	const u32 *data32 = data;
	void __iomem *bar4 = dev->remap[2]; /* remap == bar0, bar2, bar4 */
	int size32 = (size8 + 3) >> 2;

	printk("programming with bar4 @ %x, vaddr %p\n",
	       dev->area[2]->start, bar4);

	if (0) {
		/*
		 * Hmmm.... revers bits for xilinx images?
		 * We can't do in kernel space anyways, as the pages are RO
		 */
		u8 *d8 = (u8 *)data8; /* Horrible: kill const */
		for (i = 0; i < size8; i++) {
			val8 = d8[i];
			d8[i] =  0
				| ((val8 & 0x80) >> 7)
				| ((val8 & 0x40) >> 5)
				| ((val8 & 0x20) >> 3)
				| ((val8 & 0x10) >> 1)
				| ((val8 & 0x08) << 1)
				| ((val8 & 0x04) << 3)
				| ((val8 & 0x02) << 5)
				| ((val8 & 0x01) << 7);
		}
	}

	/* Do real stuff */
	writel(0x00, bar4 + FCL_CLK_DIV);
	writel(0x40, bar4 + FCL_CTRL); /* Reset */
	i = readl(bar4 + FCL_CTRL);
	if (i != 0x40) {
		printk(KERN_ERR "%s: %i: error\n", __func__, __LINE__);
		return;
	}
	writel(0x00, bar4 + FCL_CTRL);

	writel(0x00, bar4 + FCL_IRQ); /* clear pending irq */

	switch(size8 & 3) {
	case 3: ctrl = 0x116; break;
	case 2: ctrl = 0x126; break;
	case 1: ctrl = 0x136; break;
	case 0: ctrl = 0x106; break;
	}
	writel(ctrl, bar4 + FCL_CTRL);

	writel(0x00, bar4 + FCL_CLK_DIV); /* again? maybe 1 or 2? */

	writel(0x00, bar4 + FCL_TIMER_CTRL); /* "disable FCL timer func" */

	writel(0x10, bar4 + FCL_TIMER_0); /* "pulse width" */
	writel(0x00, bar4 + FCL_TIMER_1);

	/* Set delay before data and clock is applied by FCL after SPRI_STATUS is
		detected being assert.
	*/
	writel(0x08, bar4 + FCL_TIMER2_0); /* "delay before data/clock..." */
	writel(0x00, bar4 + FCL_TIMER2_1);
	writel(0x17, bar4 + FCL_EN); /* "output enable" */

	ctrl |= 0x01; /* "start FSM configuration" */
	writel(ctrl, bar4 + FCL_CTRL);

	while(size32 > 0)
	{
		/* Check to see if FPGA configuation has error */
		i = readl(bar4 + FCL_IRQ);
		if ( (i & 8) && wrote) {
			done = 1;
			printk("%s: %i: done after %i\n", __func__, __LINE__,
				wrote);
		} else if ( (i & 0x4) && !done) {
			printk("%s: %i: error after %i\n", __func__, __LINE__,
				wrote);
			return;
		}

		/* Write 128 dwords into FIFO at a time. */
		for (i = 0; size32 && i < 128; i++) {
			writel(*data32, bar4 + FCL_FIFO);
			data32++; size32--; wrote++;
			udelay(10);
		}
	}

	writel(0x186, bar4 + FCL_CTRL); /* "last data written" */

	j = jiffies + 2 * HZ;
	/* Wait for DONE interrupt  */
	while(!done) {
		i = readl(bar4 + FCL_IRQ);
		if (i & 0x8) {
			printk("%s: %i: done after %i\n", __func__, __LINE__,
			       wrote);
			done = 1;
		} else if( (i & 0x4) && !done) {
			printk("%s: %i: error after %i\n", __func__, __LINE__,
			       wrote);
			return;
		}
		if (time_after(jiffies, j)) {
			printk("%s: %i: tout after %i\n", __func__, __LINE__,
			       wrote);
			return;
		}
	}
	return;
}



