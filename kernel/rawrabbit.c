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
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/ioctl.h>

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
	if (dev->devsel.subvendor == RR_DEVSEL_UNUSED) {
		dev->id_table->subvendor = PCI_ANY_ID;
		dev->id_table->subdevice = PCI_ANY_ID;
	} else {
		dev->id_table->subvendor = dev->devsel.subvendor;
		dev->id_table->subdevice = dev->devsel.subdevice;
	}
	dev->id_table->vendor = dev->devsel.vendor;
	dev->id_table->device = dev->devsel.device;
}

static int  rr_pciprobe (struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct rr_dev *dev = &rr_dev;
	int ret = 0;

	printk("%s\n", __func__);

	spin_lock(&dev->lock);
	if (dev->devcount)
		ret = -EBUSY;
	else
		dev->devcount++;
	wmb();
	spin_unlock(&dev->lock);

	return 0;
}

static void rr_pciremove(struct pci_dev *pdev)
{
	struct rr_dev *dev = &rr_dev;

	/* This function is called when the pcidrv is removed */
	printk("%s\n", __func__);

	spin_lock(&dev->lock);
	dev->devcount--;
	if (dev->devcount != 0)
		printk(KERN_ERR "%s: devount didn't drop to 0 (%i)\n",
		       __func__, dev->devcount);
	spin_unlock(&dev->lock);
}

static struct pci_driver rr_pcidrv = {
	.name = "rawrabbit",
	.id_table = rr_idtable,
	.probe = rr_pciprobe,
	.remove = rr_pciremove,
};

/* There is only one device by now; I didn't find how to associate to pcidev */
static struct rr_dev rr_dev = {
	.pci_driver = &rr_pcidrv,
	.id_table = rr_idtable,
};

/*
 * The ioctl method is the one used for strange stuff (see docs)
 */
static long rr_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	return -EOPNOTSUPP;
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
	wmb();
	spin_unlock(&dev->lock);

	return 0;
}

static int rr_release(struct inode *ino, struct file *f)
{
	struct rr_dev *dev = f->private_data;

	spin_lock(&dev->lock);
	dev->usecount--;
	wmb();
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

	/* prepare registration of the pci driver according to parameters */
	dev->devsel.vendor = rr_vendor;
	dev->devsel.device = rr_device;
	dev->devsel.subvendor = RR_DEVSEL_UNUSED;
	dev->devsel.bus = RR_DEVSEL_UNUSED;
	rr_fill_table(dev);

	ret = misc_register(&rr_misc);
	if (ret < 0) {
		printk(KERN_ERR "%s: Can't register misc device\n",
		       KBUILD_MODNAME);
		return ret;
	}

	ret = pci_register_driver(&rr_pcidrv);
	if (ret < 0) {
		printk(KERN_ERR "%s: Can't register pci driver\n",
		       KBUILD_MODNAME);
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
