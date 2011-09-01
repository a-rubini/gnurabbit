/*
 * Spec-demo core driver. This has been copied from rawrabbit-core.c
 * and then modified. To keep differences small it still uses "rr_" as
 * a prefix.
 *
 * Copyright (C) 2010,2011 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>

#include "rawrabbit.h"
#include "compat.h"

static int rr_vendor = RR_DEFAULT_VENDOR;
static int rr_device = RR_DEFAULT_DEVICE;
module_param_named(vendor, rr_vendor, int, 0);
module_param_named(device, rr_device, int, 0);

static int rr_bufsize = RR_DEFAULT_BUFSIZE;
module_param_named(bufsize, rr_bufsize, int, 0);

struct rr_dev rr_dev; /* defined later */

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

	/* This needs no locks, as it's called in semaphorized process ctxt */
	if (dev->flags & RR_FLAG_REGISTERED) {
		pci_unregister_driver(dev->pci_driver);
		dev->flags &= ~ RR_FLAG_REGISTERED;
	}

	rr_fill_table(dev);

	/* Use the completion mechanism to be notified of probes */
	init_completion(&dev->complete);
	ret = pci_register_driver(dev->pci_driver);
	if (ret == 0)
		dev->flags |= RR_FLAG_REGISTERED;

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

/* The probe and remove function can't get locks, as it's already locked */
static int rr_pciprobe (struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct rr_dev *dev = &rr_dev;
	int i;

	/* Only manage one device, refuse further probes */
	if (dev->pdev)
		return -EBUSY;
	/* vendor/device and subvendor/subdevice have already been matched */
	if (dev->devsel->bus != RR_DEVSEL_UNUSED) {
		if (dev->devsel->bus != pdev->bus->number)
			return -ENODEV;
		if (dev->devsel->devfn != pdev->devfn)
			return -ENODEV;
	}
	/* Record the informaztion in the local structure anyways */
	dev->devsel->subvendor = pdev->subsystem_vendor;
	dev->devsel->subdevice = pdev->subsystem_device;
	dev->devsel->bus = pdev->bus->number;
	dev->devsel->devfn = pdev->devfn;

	/* The firmware is a module parameter, if unset use default */
	dev->fwname = rr_fwname;
	if (!dev->fwname[0])
		dev->fwname = "spec_top.bin";

	i = pci_enable_device(pdev);
	if (i < 0)
	    return i;

	dev->pdev = pdev;

	if (0) {	/* Print some information about the bars */
		int i;
		struct resource *r;

		for (i = 0; i < 6; i++) {
			r = pdev->resource + i;
			printk("%s: resource %i: %llx-%llx - %08lx\n",
			       __func__, i,
			       (long long)r->start,
			       (long long)r->end, r->flags);
		}
	}

	/* Record the three bars and possibly remap them */
	for (i = 0; i < 3; i++) {
		struct resource *r = pdev->resource + (2 * i);
		if (!r->start)
			continue;
		dev->area[i] = r;
		if (r->flags & IORESOURCE_MEM)
			dev->remap[i] = ioremap(r->start,
						r->end + 1 - r->start);
	}

	complete(&dev->complete);

	/*
	 * Finally, ask for a copy of the firmware for this device,
	 * _and_ a copy of the lm32 program
	 */
	dev->load_program = spec_ask_program;
	rr_ask_firmware(dev);

	return 0;
}

/* This function is called when the pcidrv is removed, with lock held */
static void rr_pciremove(struct pci_dev *pdev)
{
	struct rr_dev *dev = &rr_dev;
	int i;

	for (i = 0; i < 3; i++) {
		iounmap(dev->remap[i]);		/* safe for NULL ptrs */
		dev->remap[i] = NULL;
		dev->area[i] = NULL;
	}
	release_firmware(dev->fw);
	dev->fw = NULL;
	dev->pdev = NULL;
}

static struct pci_driver rr_pcidrv = {
	.name = "rawrabbit",
	.id_table = rr_idtable,
	.probe = rr_pciprobe,
	.remove = rr_pciremove,
};

/* There is only one device by now; I didn't find how to associate to pcidev */
static struct rr_devsel rr_devsel;
/* The device can't be static. I need it in the module parameter */
struct rr_dev rr_dev = {
	.pci_driver = &rr_pcidrv,
	.id_table = rr_idtable,
	.q = __WAIT_QUEUE_HEAD_INITIALIZER(rr_dev.q),
	.mutex = __MUTEX_INITIALIZER(rr_dev.mutex),
	.devsel = &rr_devsel,
	.work = __WORK_INITIALIZER(rr_dev.work, rr_load_firmware),
};


/*
 * These functions are (inlined) helpers for ioctl
 */
static int rr_do_read_mem(struct rr_dev *dev, struct rr_iocmd *iocmd)
{
	int bar = __RR_GET_BAR(iocmd->address) / 2;
	int off = __RR_GET_OFF(iocmd->address);
	struct resource *r = dev->area[bar];

	if (off >= r->end - r->start + 1)
		return -ENOMEDIUM;

	switch(iocmd->datasize) {
	case 1:
		iocmd->data8 = readb(dev->remap[bar] + off);
		break;
	case 2:
		if (off & 1)
			return -EIO;
		iocmd->data16 = readw(dev->remap[bar] + off);
		break;
	case 4:
		if (off & 3)
			return -EIO;
		iocmd->data32 = readl(dev->remap[bar] + off);
		break;
	case 8:
		if (off & 7)
			return -EIO;
		iocmd->data64 = readq(dev->remap[bar] + off);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int rr_do_write_mem(struct rr_dev *dev, struct rr_iocmd *iocmd)
{
	int bar = __RR_GET_BAR(iocmd->address) / 2;
	int off = __RR_GET_OFF(iocmd->address);
	struct resource *r = dev->area[bar];

	if (off >= r->end - r->start + 1)
		return -ENOMEDIUM;

	switch(iocmd->datasize) {
	case 1:
		writeb(iocmd->data8, dev->remap[bar] + off);
		break;
	case 2:
		if (off & 1)
			return -EIO;
		writew(iocmd->data16, dev->remap[bar] + off);
		break;
	case 4:
		if (off & 3)
			return -EIO;
		writel(iocmd->data32, dev->remap[bar] + off);
		break;
	case 8:
		if (off & 7)
			return -EIO;
		writeq(iocmd->data64, dev->remap[bar] + off);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int rr_do_read_io(struct rr_dev *dev, struct rr_iocmd *iocmd)
{
	int bar = __RR_GET_BAR(iocmd->address) / 2;
	int off = __RR_GET_OFF(iocmd->address);
	struct resource *r = dev->area[bar];

	if (off >= r->end - r->start + 1)
		return -ENOMEDIUM;

	switch(iocmd->datasize) {
	case 1:
		iocmd->data8 = inb(r->start + off);
		break;
	case 2:
		if (off & 1)
			return -EIO;
		iocmd->data16 = inw(r->start + off);
		break;
	case 4:
		if (off & 3)
			return -EIO;
		iocmd->data32 = inl(r->start + off);
		break;
	case 8:
		if (off & 7)
			return -EIO;
		/* assume little-endian bus */
		iocmd->data64 = inl(r->start + off);
		iocmd->data64 |= (__u64)(inl(r->start + off + 4)) << 32;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int rr_do_write_io(struct rr_dev *dev, struct rr_iocmd *iocmd)
{
	int bar = __RR_GET_BAR(iocmd->address) / 2 ;
	int off = __RR_GET_OFF(iocmd->address);
	struct resource *r = dev->area[bar];

	if (off >= r->end - r->start + 1)
		return -ENOMEDIUM;

	switch(iocmd->datasize) {
	case 1:
		outb(iocmd->data8,  r->start + off);
		break;
	case 2:
		if (off & 1)
			return -EIO;
		outw(iocmd->data16, r->start + off);
		break;
	case 4:
		if (off & 3)
			return -EIO;
		outl(iocmd->data32, r->start + off);
		break;
	case 8:
		if (off & 7)
			return -EIO;
		/* assume little-endian bus */
		outl(iocmd->data64, r->start + off);
		outl(iocmd->data64 >> 32, r->start + off + 4);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/* This helper is called for dmabuf operations */
static int rr_do_iocmd_dmabuf(struct rr_dev *dev, unsigned int cmd,
		       struct rr_iocmd *iocmd)
{
	int off = __RR_GET_OFF(iocmd->address);
	if (off >= rr_bufsize)
		return -ENOMEDIUM;

	switch(iocmd->datasize) {
	case 1:
		if (cmd == RR_WRITE)
			*(u8 *)(dev->dmabuf + off) = iocmd->data8;
		else
			iocmd->data8 = *(u8 *)(dev->dmabuf + off);
		break;
	case 2:
		if (off & 1)
			return -EIO;
		if (cmd == RR_WRITE)
			*(u16 *)(dev->dmabuf + off) = iocmd->data16;
		else
			iocmd->data16 = *(u16 *)(dev->dmabuf + off);
		break;
	case 4:
		if (off & 3)
			return -EIO;
		if (cmd == RR_WRITE)
			*(u32 *)(dev->dmabuf + off) = iocmd->data32;
		else
			iocmd->data32 = *(u32 *)(dev->dmabuf + off);
		break;
	case 8:
		if (off & 7)
			return -EIO;
		if (cmd == RR_WRITE)
			*(u64 *)(dev->dmabuf + off) = iocmd->data64;
		else
			iocmd->data64 = *(u64 *)(dev->dmabuf + off);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int rr_do_iocmd(struct rr_dev *dev, unsigned int cmd,
		       struct rr_iocmd *iocmd)
{
	int bar;
	unsigned off;
	struct resource *r;

	bar = __RR_GET_BAR(iocmd->address);
	off = __RR_GET_OFF(iocmd->address);

	if (!rr_is_valid_bar(iocmd->address))
		return -EINVAL;

	if (rr_is_dmabuf_bar(iocmd->address))
		return rr_do_iocmd_dmabuf(dev, cmd, iocmd);

	bar /= 2;			/* use 0,1,2 as index */
	r = dev->area[bar];

	if (!r)
		return -ENODEV;

	if (likely(r->flags & IORESOURCE_MEM)) {
		if (cmd == RR_READ)
			return rr_do_read_mem(dev, iocmd);
		else
			return rr_do_write_mem(dev, iocmd);
	}
	if (likely(r->flags & IORESOURCE_IO)) {
		if (cmd == RR_READ)
			return rr_do_read_io(dev, iocmd);
		else
			return rr_do_write_io(dev, iocmd);
	}
	return -EIO;
}


/*
 * The ioctl method is the one used for strange stuff (see docs)
 */
static long rr_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct rr_dev *dev = f->private_data;
	int size = _IOC_SIZE(cmd); /* the size bitfield in cmd */
	int ret = 0;
	void *addr;
	u32 __user *uptr = (u32 __user *)arg;

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

	/* serialize the switch with other processes */
	mutex_lock(&dev->mutex);

	switch(cmd) {

	case RR_DEVSEL:
		/* If we are the only user, change device requested id */
		if (dev->usecount > 1) {
			printk("usecount %i\n", dev->usecount);
			ret = -EBUSY;
			break;
		}
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
		if (!dev->pdev) {
			ret = -ENODEV;
			break;
		}
		memset(&karg.devsel, 0, sizeof(karg.devsel));
		karg.devsel.vendor = dev->pdev->vendor;
		karg.devsel.device = dev->pdev->device;
		karg.devsel.subvendor = dev->pdev->subsystem_vendor;
		karg.devsel.subdevice = dev->pdev->subsystem_device;
		karg.devsel.bus = dev->pdev->bus->number;
		karg.devsel.devfn = dev->pdev->devfn;
		break;

	case RR_READ:	/* Read a "word" of memory */
	case RR_WRITE:	/* Write a "word" of memory */
		ret = rr_do_iocmd(dev, cmd, &karg.iocmd);
		break;

	case RR_GETDMASIZE:	/* Return the current dma size */
		ret = rr_bufsize;
		break;

	case RR_GETPLIST:	/* Return the page list */

		/* Since we assume PAGE_SIZE is 4096, check at compile time */
		if (PAGE_SIZE != RR_PLIST_SIZE) {
			extern void __page_size_is_not_4096(void);
			__page_size_is_not_4096(); /* undefined symbol */
		}

		if (!access_ok(VERIFY_WRITE, arg, RR_PLIST_SIZE)) {
			ret = -EFAULT;
			break;
		}
		for (addr = dev->dmabuf; addr - dev->dmabuf < rr_bufsize;
		     addr += PAGE_SIZE) {
			if (0) {
				printk("page @ %p - pfn %08lx\n", addr,
				       page_to_pfn(vmalloc_to_page(addr)));
			}
			__put_user(page_to_pfn(vmalloc_to_page(addr)), uptr);
			uptr++;
		}
		break;

	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	/* finally, copy data to user space and return */
	mutex_unlock(&dev->mutex);
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

	mutex_lock(&dev->mutex);
	dev->usecount++;
	mutex_unlock(&dev->mutex);

	return 0;
}

static int rr_release(struct inode *ino, struct file *f)
{
	struct rr_dev *dev = f->private_data;

	mutex_lock(&dev->mutex);
	dev->usecount--;
	mutex_unlock(&dev->mutex);

	return 0;
}

static int rr_mmap(struct file *f, struct vm_area_struct *vma)
{
	return -EOPNOTSUPP;
}

static ssize_t rr_read(struct file *f, char __user *buf, size_t count,
		       loff_t *offp)
{
	struct rr_dev *dev = f->private_data;
	void *base;
	loff_t pos = *offp;
	int bar, off, size;

	bar = __RR_GET_BAR(pos) / 2; /* index in the array */
	off = __RR_GET_OFF(pos);
	if (0)
		printk("%s: pos %llx = bar %x off %x\n", __func__, pos,
		       bar*2, off);
	if (!rr_is_valid_bar(pos))
		return -EINVAL;

	/* reading the DMA buffer is trivial, so do it first */
	if (RR_IS_DMABUF(pos)) {
		base = dev->dmabuf;
		if (off >= rr_bufsize)
			return 0; /* EOF */
		if (off + count > rr_bufsize)
			count = rr_bufsize - off;
		if (copy_to_user(buf, base + off, count))
			return -EFAULT;
		*offp += count;
		return count;
	}

	/* inexistent or I/O ports: EINVAL */
	if (!dev->remap[bar])
		return -EINVAL;
	base = dev->remap[bar];

	/* valid on-board area: enforce sized access if size is 1,2,4,8 */
	size = dev->area[bar]->end + 1 - dev->area[bar]->start;
	if (off >= size)
		return -EIO; /* it's not memory, an error is better than EOF */
	if (count + off > size)
		count = size - off;
	switch (count) {
	case 1:
		if (put_user(readb(base + off), (u8 *)buf))
			return -EFAULT;
		break;
	case 2:
		if (put_user(readw(base + off), (u16 *)buf))
			return -EFAULT;
		break;
	case 4:
		if (put_user(readl(base + off), (u32 *)buf))
			return -EFAULT;
		break;
	case 8:
		if (put_user(readq(base + off), (u64 *)buf))
			return -EFAULT;
		break;
	default:
		if (copy_to_user(buf, base + off, count))
			return -EFAULT;
	}
	*offp += count;
	return count;
}

static ssize_t rr_write(struct file *f, const char __user *buf, size_t count,
		 loff_t *offp)
{
	struct rr_dev *dev = f->private_data;
	void *base;
	loff_t pos = *offp;
	int bar, off, size;
	union {u8 d8; u16 d16; u32 d32; u64 d64;} data;
	bar = __RR_GET_BAR(pos) / 2; /* index in the array */
	off = __RR_GET_OFF(pos);
	if (!rr_is_valid_bar(pos))
		return -EINVAL;

	/* writing the DMA buffer is trivial, so do it first */
	if (RR_IS_DMABUF(pos)) {
		base = dev->dmabuf;
		if (off >= rr_bufsize)
			return -ENOSPC;
		if (off + count > rr_bufsize)
			count = rr_bufsize - off;
		if (copy_from_user(base + off, buf, count))
			return -EFAULT;
		*offp += count;
		return count;
	}

	/* inexistent or I/O ports: EINVAL */
	if (!dev->remap[bar])
		return -EINVAL;
	base = dev->remap[bar];

	/* valid on-board area: enforce sized access if size is 1,2,4,8 */
	size = dev->area[bar]->end + 1 - dev->area[bar]->start;
	if (off >= size)
		return -EIO; /* it's not memory, an error is better than EOF */
	if (count + off > size)
		count = size - off;
	switch (count) {
	case 1:
		if (get_user(data.d8, (u8 *)buf))
			return -EFAULT;
		writeb(data.d8, base + off);
		break;
	case 2:
		if (get_user(data.d16, (u16 *)buf))
			return -EFAULT;
		writew(data.d16, base + off);
		break;
	case 4:
		if (get_user(data.d32, (u32 *)buf))
			return -EFAULT;
		writel(data.d32, base + off);
		break;
	case 8:
		/* while put_user_8 exists, get_user_8 does not */
		if (copy_from_user(&data.d64, buf, count))
			return -EFAULT;
		writeq(data.d64, base + off);
		break;
	default:
		if (copy_from_user(base + off, buf, count))
			return -EFAULT;
	}
	*offp += count;
	return count;
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

	if (rr_bufsize > RR_MAX_BUFSIZE) {
		printk(KERN_WARNING "rawrabbit: too big a size, using 0x%x\n",
		       RR_MAX_BUFSIZE);
		rr_bufsize = RR_MAX_BUFSIZE;
	}

	dev->dmabuf = __vmalloc(rr_bufsize, GFP_KERNEL | __GFP_ZERO,
				PAGE_KERNEL);
	if (!dev->dmabuf)
		return -ENOMEM;

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
	struct rr_dev *dev = &rr_dev;

	pci_unregister_driver(&rr_pcidrv);
	misc_deregister(&rr_misc);
	vfree(dev->dmabuf);
}

module_init(rr_init);
module_exit(rr_exit);

MODULE_LICENSE("GPL");
