/*
 * Raw I/O interface for PCI or PCI express interfaces
 *
 * Copyright (C) 2010 Alessandro Rubini <rubini@gnudd.com>
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project and has been sponsored
 * by CERN, the European Institute for Nuclear Research.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/completion.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>

#include "rawrabbit.h"

static int rr_vendor = RR_DEFAULT_VENDOR;
static int rr_device = RR_DEFAULT_DEVICE;
module_param_named(vendor, rr_vendor, int, 0);
module_param_named(device, rr_device, int, 0);

static struct rr_dev rr_dev; /* defined later */

/*
 * We have a PCI driver, used to access the BAR areas.
 * One device id only is supported. 
 */

static struct pci_device_id rr_idtable[2]; /* last must be zero */

static void rr_fill_table(struct rr_dev *dev)
{
	if (dev->devsel->subvendor == RR_DEVSEL_UNUSED) {
		dev->id_table->subvendor = PCI_ANY_ID;
		dev->id_table->subdevice = PCI_ANY_ID;
	} else {
		dev->id_table->subvendor = dev->devsel->subvendor;
		dev->id_table->subdevice = dev->devsel->subdevice;
	}
	dev->id_table->vendor = dev->devsel->vendor;
	dev->id_table->device = dev->devsel->device;
}

static int rr_fill_table_and_probe(struct rr_dev *dev)
{
	int ret;

	if (dev->pdev)
		pci_unregister_driver(dev->pci_driver);

	rr_fill_table(dev);

	/* Use the completion mechanism to be notified of probes */
	init_completion(&dev->complete);
	ret = pci_register_driver(dev->pci_driver);
	if (ret < 0) {
		printk(KERN_ERR "%s: Can't register pci driver\n",
		       KBUILD_MODNAME);
		return ret;
	}
	/* This ret is 0 (timeout) or positive */
	ret = wait_for_completion_timeout(&dev->complete, RR_PROBE_TIMEOUT);
	if (!ret) {
		printk("%s: Warning: no device found\n", __func__);
	}
	return ret;
}

static int  rr_pciprobe (struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct rr_dev *dev = &rr_dev;
	int ret = 0;

	printk("%s\n", __func__);

	/* Only manage one device, refuse further probes */
	spin_lock(&dev->lock);
	if (dev->pdev)
		ret = -EBUSY;
	else
		dev->pdev = pdev;
	spin_unlock(&dev->lock);

	/* FIXME: the check for bus/devfn is missing */

	if (dev->pdev == pdev)
		complete(&dev->complete);

	return 0;
}

static void rr_pciremove(struct pci_dev *pdev)
{
	struct rr_dev *dev = &rr_dev;

	/* This function is called when the pcidrv is removed */
	printk("%s\n", __func__);

	spin_lock(&dev->lock);
	dev->pdev = NULL;
	spin_unlock(&dev->lock);
}

static struct pci_driver rr_pcidrv = {
	.name = "rawrabbit",
	.id_table = rr_idtable,
	.probe = rr_pciprobe,
	.remove = rr_pciremove,
};

/* There is only one device by now; I didn't find how to associate to pcidev */
static struct rr_devsel rr_devsel;
static struct rr_dev rr_dev = {
	.pci_driver = &rr_pcidrv,
	.id_table = rr_idtable,
	.devsel = &rr_devsel,
};

/*
 * The ioctl method is the one used for strange stuff (see docs)
 */
static long rr_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct rr_dev *dev = f->private_data;
	int size = _IOC_SIZE(cmd); /* the size bitfield in cmd */
	int ret = 0;

	/* local copies: use a union to save stack space */
	union {
		struct rr_iocmd iocmd;
		struct rr_devsel devsel;
	} karg;

	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOIOCTLCMD before verify_area()
	 */
	if (_IOC_TYPE(cmd) != __RR_IOC_MAGIC)
		return -ENOIOCTLCMD;

	/*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Dir' is user-oriented, while
	 * verify_area is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
	if (_IOC_DIR(cmd) & _IOC_READ) {
		if (!access_ok(VERIFY_WRITE, (void *)arg, size))
			return -EFAULT;
	}
	else if (_IOC_DIR(cmd) & _IOC_WRITE) {
		if (!access_ok(VERIFY_READ, (void *)arg, size))
			return -EFAULT;
	}

	/* first, retrieve data from userspace */
	if ((_IOC_DIR(cmd) & _IOC_WRITE) && (size <= sizeof(karg)))
		if (copy_from_user(&karg, (void *)arg, size))
			return -EFAULT;

	switch(cmd) {

	case RR_DEVSEL:
		/* If we are the only user, change device requested id */
		spin_lock(&dev->lock);
		if (dev->usecount > 1)
			ret = -EBUSY;
		spin_unlock(&dev->lock);
		if (ret < 0)
			break;
		/* Warning: race: we can't take the lock */
		*dev->devsel = karg.devsel;
		ret = rr_fill_table_and_probe(dev);
		if (!ret)
			ret = -ENODEV; /* timeout */
		else if (ret > 0)
			ret = 0; /* success */
		break;

	case RR_DEVGET:
		/* Return to user space the id of the current device */
		spin_lock(&dev->lock);
		if (!dev->pdev) {
			spin_unlock(&dev->lock);
			return -ENODEV;
		}
		memset(&karg.devsel, 0, sizeof(karg.devsel));
		karg.devsel.vendor = dev->pdev->vendor;
		karg.devsel.device = dev->pdev->device;
		karg.devsel.subvendor = dev->pdev->subsystem_vendor;
		karg.devsel.subdevice = dev->pdev->subsystem_device;
		karg.devsel.bus = dev->pdev->bus->number;
		karg.devsel.devfn = dev->pdev->devfn;
		spin_unlock(&dev->lock);
		break;

	default:
		return -ENOIOCTLCMD;
	}

	/* finally, copy data to user space and return */
	if (ret < 0)
		return ret;
	if ((_IOC_DIR(cmd) & _IOC_READ) && (size <= sizeof(karg)))
		if (copy_to_user((void *)arg, &karg, size))
			return -EFAULT;
	return ret;
}

/*
 * Other fops are more conventional
 */
static int rr_open(struct inode *ino, struct file *f)
{
	struct rr_dev *dev = &rr_dev;
	f->private_data = dev;

	spin_lock(&dev->lock);
	dev->usecount++;
	spin_unlock(&dev->lock);

	return 0;
}

static int rr_release(struct inode *ino, struct file *f)
{
	struct rr_dev *dev = f->private_data;

	spin_lock(&dev->lock);
	dev->usecount--;
	spin_unlock(&dev->lock);

	return 0;
}

static int rr_mmap(struct file *f, struct vm_area_struct *vma)
{
	return -EOPNOTSUPP;
}

static ssize_t rr_read(struct file *f, char __user *buf, size_t count,
		       loff_t *offp)
{
	return 0;
}

static ssize_t rr_write(struct file *f, const char __user *buf, size_t count,
		 loff_t *offp)
{
	return 0;
}

static struct file_operations rr_fops = {
	.open = rr_open,
	.release = rr_release,
	.read = rr_read,
	.write = rr_write,
	.mmap = rr_mmap,
	.unlocked_ioctl = rr_ioctl,
};

/* Registering and unregistering the misc device */
static struct miscdevice rr_misc = {
	.minor = 42,
	.name = "rawrabbit",
	.fops = &rr_fops,
};

/* init and exit */
static int rr_init(void)
{
	int ret;
	struct rr_dev *dev = &rr_dev; /* always use dev as pointer */

	/* misc device, that's trivial */
	ret = misc_register(&rr_misc);
	if (ret < 0) {
		printk(KERN_ERR "%s: Can't register misc device\n",
		       KBUILD_MODNAME);
		return ret;
	}

	/* prepare registration of the pci driver according to parameters */
	dev->devsel->vendor = rr_vendor;
	dev->devsel->device = rr_device;
	dev->devsel->subvendor = RR_DEVSEL_UNUSED;
	dev->devsel->bus = RR_DEVSEL_UNUSED;

	/* This function return < 0 on error, 0 on timeout, > 0 on success */
	ret = rr_fill_table_and_probe(dev);
	if (ret < 0) {
		misc_deregister(&rr_misc);
		return ret;
	}

	return 0;
}

static void rr_exit(void)
{
	pci_unregister_driver(&rr_pcidrv);
	misc_deregister(&rr_misc);
}

module_init(rr_init);
module_exit(rr_exit);

MODULE_LICENSE("GPL");
