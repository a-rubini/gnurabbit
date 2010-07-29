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
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/ioctl.h>

#include "rawrabbit.h"

static int rr_vendor = RR_DEFAULT_VENDOR;
static int rr_device = RR_DEFAULT_DEVICE;
module_param_named(vendor, rr_vendor, int, 0);
module_param_named(device, rr_device, int, 0);

/*
 * The ioctl method is the one used for strange stuff
 */
long rr_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	return -EOPNOTSUPP;
}

/* long (*compat_ioctl) (struct file *, unsigned int, unsigned long); */

/* Other fops are more conventional */
int rr_open(struct inode *ino, struct file *f)
{
	return -ENODEV;
}

int rr_release(struct inode *ino, struct file *f)
{
	return 0;
}

int rr_mmap(struct file *f, struct vm_area_struct *vma)
{
	return -EOPNOTSUPP;
}

ssize_t rr_read(struct file *f, char __user *buf, size_t count, loff_t *offp)
{
	return 0;
}

ssize_t rr_write(struct file *f, const char __user *buf, size_t count,
		 loff_t *offp)
{
	return 0;
}

struct file_operations rr_fops = {
	.open = rr_open,
	.release = rr_release,
	.read = rr_read,
	.write = rr_write,
	.mmap = rr_mmap,
	.unlocked_ioctl = rr_ioctl,
};

/* Registering and unregistering the misc device */
struct miscdevice rr_misc = {
	.minor = 42,
	.name = "rawrabbit",
	.fops = &rr_fops,
};

static int rr_init(void)
{
	int ret;

	ret = misc_register(&rr_misc);
	if (ret < 0) {
		printk(KERN_ERR "%s: Can't register misc device\n",
		       KBUILD_MODNAME);
		return ret;
	}
	return 0;
}

static void rr_exit(void)
{
	misc_deregister(&rr_misc);
}

module_init(rr_init);
module_exit(rr_exit);

MODULE_LICENSE("GPL");
