
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/hardirq.h>
#include <linux/workqueue.h>
#include <linux/firmware.h>
#include <linux/jiffies.h>
#include <linux/ctype.h>
#include <linux/mutex.h>

#include "rawrabbit.h"
#include "compat.h"
#include "loader-ll.h"

#ifndef CONFIG_FW_LOADER
#warning "CONFIG_FW_LOADER is need in kernel configuration."
#warning "Compiling anyways, but won't be able to load firmware"
#endif

/* a few prototypes, to avoid many diff lines from previous versions */
static int __rr_gennum_load(struct rr_dev *dev, const void *data, int size);
static int rr_expand_name(struct rr_dev *dev, char *outname);

/* I need to be notified when I get written, so "charp" won't work */
static int param_set_par_fwname(const char *val, struct kernel_param *kp)
{
	const char *prev = *(const char **)kp->arg;
	int ret = param_set_charp(val, kp);
	extern struct rr_dev rr_dev; /* global: no good */
	struct rr_dev *dev = &rr_dev;
	static char fwname[RR_MAX_FWNAME_SIZE];

	if (ret)
		  return ret;
	ret = rr_expand_name(dev, fwname);
	if (ret) {
		/*
		 * bad it went: refuse this string and restore prev one
		 * Looks like there's a problem here: the previous string
		 * was not freed, or it's me who didn't see it?
		 */
		kfree(*(const char **)kp->arg);
		*(const char **)kp->arg = prev;
		return ret;
	}
	/*
	 * Use the new firmware immediately at this time. We are in user
	 * context, so take the mutex to avoid contention with ioctl.
	 * Note that if we are not bound (like at insmod time) we don't
	 * want to do the actual loading
	 */
	if (dev->pdev) {
		mutex_lock(&dev->mutex);
		rr_ask_firmware(dev);
		mutex_unlock(&dev->mutex);
	}
	return 0;
}

static int param_get_par_fwname(char *buffer, struct kernel_param *kp)
{
	int ret = param_get_charp(buffer, kp);
	return ret;
}

//#define param_check_par_fwname(name, p) __param_check(name, p, char *)

char *rr_fwname = "";
module_param_named(fwname, rr_fwname, charp, 0644);

static int rr_expand_name(struct rr_dev *dev, char *outname)
{
	struct pci_dev *pdev = dev->pdev;
	char *si, *so = outname;

	for (si = dev->fwname; *si ; si++) {
		if (so - outname >= RR_MAX_FWNAME_SIZE)
			return -ENOSPC;
		if (*si != '%') {
			*so++ = *si;
			continue;
		}
		si++;
		if (so - outname + 9 >= RR_MAX_FWNAME_SIZE)
			return -ENOSPC;
		switch(*si) {
		case 'P': /* PCI vendor:device */;
			so += sprintf(so, "%04x:%04x",
				pdev->vendor, pdev->device);
			break;
		case 'p': /* PCI subvendor:subdevice */;
			so += sprintf(so, "%04x:%04x",
				pdev->subsystem_vendor, pdev->subsystem_device);
			break;
		case 'b': /* BUS id */
			so += sprintf(so, "%04x:%04x",
				pdev->bus->number, pdev->devfn);
			break;
		case '%':
			*so++ = '%';
		default:
			return -EINVAL;
		}
	}
	/* terminate and remove trailing spaces (includes newlines) */
	*so = '\0';
	while (isspace(*--so))
		*so = '\0';
	return 0;
}

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
	int ret;

	dev->fw = fw;

	__rr_report_env(__func__); /* Here, preempt count is 0: good! */

	if (fw) {
		pr_info("%s: got firmware file, %i (0x%x) bytes\n", __func__,
			fw ? fw->size : 0, fw ? fw->size : 0);
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
		ret = __rr_gennum_load(dev, fw->data, fw->size);
	} else {
		pr_err("%s: not loading firmware: this is not a GN4124\n",
			   __func__);
		ret = -ENODEV;
	}
	/* At this point, we can releae the firmware we got */
	release_firmware(dev->fw);
	if (ret)
		pr_err("%s: loading returned error %i\n", __func__, ret);
	dev->fw = NULL;

	/* Tell we are done, so the spec can load the next blob */
	complete(&dev->fw_load);
}

/*
 * We want to run the actual loading from a work queue, to have a known
 * loading environment, especially one that can sleep. The function
 * pointer is already in the work structure, set at compile time from
 * rawrabbit-core.c .
 */
void rr_load_firmware(struct work_struct *work)
{
	struct rr_dev *dev = container_of(work, struct rr_dev, work);
	struct pci_dev *pdev = dev->pdev;
	static char fwname[RR_MAX_FWNAME_SIZE];
	int err;

	if (rr_expand_name(dev, fwname)) {
		dev_err(&pdev->dev, "Wrong fwname: \"%s\"\n", rr_fwname);
		return;
	}
	if (!strcmp(fwname, "none")) {
		printk("%s: not loading firmware \"none\"\n", __func__);
		return;
	}


	if (1)
		printk("%s: %p, %s\n", __func__, pdev, fwname);

	__rr_report_env(__func__);

	init_completion(&dev->fw_load);
	err = request_firmware_nowait(THIS_MODULE, 1, fwname, &pdev->dev,
				      __RR_GFP_FOR_RFNW(GFP_KERNEL)
				      dev, rr_loader_complete);
	printk("request firmware \"%s\": %i (%s)\n", fwname, err,
	       err ? "Error" : "Success");

	/* Now, wait for loading to end. So another binary can be loaded */
	wait_for_completion(&dev->fw_load);

	/* The spec need another binary. It does so by filling a ptr */
	if (dev->load_program)
		dev->load_program(dev);
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
 * This function doesn't actually do the actual work: another level is
 * there to allow the same code to be run both in kernel and user space.
 * Thus, actual access to registers has been split to loader_low_level(),
 * in loader-ll.c (aided by loader-ll.h).
 */
static int __rr_gennum_load(struct rr_dev *dev, const void *data, int size8)
{
	int i, done = 0, wrote = 0;
	unsigned long j;
	void __iomem *bar4 = dev->remap[2]; /* remap == bar0, bar2, bar4 */

	if (!size8)
		return 0; /* no size: success */
	if (0)
		printk("programming with bar4 @ %lx,, vaddr %p\n",
		       (unsigned long)(dev->area[2]->start), bar4);

	/* Ok, now call register access, which lived elsewhere */
	wrote = loader_low_level( 0 /* unused fd */, bar4, data, size8);
	if (wrote < 0)
		return wrote;

	j = jiffies + 2 * HZ;
	/* Wait for DONE interrupt  */
	while(!done) {
		i = readl(bar4 + FCL_IRQ);
		if (i & 0x8) {
			printk("%s: done after %i writes\n", __func__,
			       wrote);
			done = 1;
		} else if( (i & 0x4) && !done) {
			printk("%s: error after %i writes\n", __func__,
			       wrote);
			return -ETIMEDOUT;
		}
		if (time_after(jiffies, j)) {
			printk("%s: timeout after %i writes\n", __func__,
			       wrote);
			return -ETIMEDOUT;
		}
	}
	return 0;
}



