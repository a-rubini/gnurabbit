
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/hardirq.h>
#include <linux/workqueue.h>
#include <linux/firmware.h>

#include "rawrabbit.h"
#include "compat.h"

#ifndef CONFIG_FW_LOADER
#warning "CONFIG_FW_LOADER is need in kernel configuration."
#warning "Compiling anyways, but won't be able to load firmware"
#endif

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

	__rr_report_env(__func__);
	dev->fw = fw;

	printk("%p: size %i (0x%x), data %p\n", fw,
	       fw ? fw->size : 0, fw ? fw->size : 0,
	       fw ? fw->data : 0);

	/* At this point, use fw->data as a linear array of fw->size bytes */
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
/* FIXME */

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
