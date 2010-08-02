/*
 * Public header for the raw I/O interface for PCI or PCI express interfaces
 *
 * Copyright (C) 2010 Alessandro Rubini <rubini@gnudd.com>
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project and has been sponsored
 * by CERN, the European Institute for Nuclear Research.
 */
#ifndef __RAWRABBIT_H__
#define __RAWRABBIT_H__
#include <linux/types.h>
#include <linux/ioctl.h>

#ifdef __KERNEL__ /* The initial part of the file is driver-internal stuff */
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/completion.h>

struct rr_devsel;

struct rr_dev {
	struct rr_devsel	*devsel;
	struct pci_driver	*pci_driver;
	struct pci_device_id	*id_table;
	struct pci_dev		*pdev;		/* non-null after pciprobe */
	spinlock_t		 lock;
	struct completion	 complete;
	struct resource		*area[3];	/* bar 0, 2, 4 */
	void			*remap[3];	/* ioremap of bar 0, 2, 4 */
	int			 usecount;
	int			 registered;
};

#define RR_PROBE_TIMEOUT	(HZ/10)		/* for pci_register_drv */

#endif /* __KERNEL__ */

/* By default, the driver registers for this vendor/devid */
#define RR_DEFAULT_VENDOR	0x1a39
#define RR_DEFAULT_DEVICE	0x0004

/* This structure is used to select the device to be accessed, via ioctl */
struct rr_devsel {
	__u16 vendor;
	__u16 device;
	__u16 subvendor;	/* RR_DEVSEL_UNUSED to ignore subvendor/dev */
	__u16 subdevice;
	__u16 bus;		/* RR_DEVSEL_UNUSED to ignore bus and devfn */
	__u16 devfn;
};

#define RR_DEVSEL_UNUSED	0xffff

/* Offsets for BAR areas in llseek() and/or ioctl */
#define RR_BAR_0		0x00000000
#define RR_BAR_2		0x20000000
#define RR_BAR_4		0x40000000
#define __RR_GET_BAR(x)		((x) >> 28)
#define __RR_GET_OFF(x)		((x) & 0x0fffffff)

struct rr_iocmd {
	__u32 address; /* bar and offset */
	__u32 datasize; /* 1 or 2 or 4 or 8 */
	union {
		__u8 data8;
		__u16 data16;
		__u32 data32;
		__u64 data64;
	};
};

/* ioctl commands */
#define __RR_IOC_MAGIC '4' /* random or so */

#define RR_DEVSEL	 _IOW(__RR_IOC_MAGIC, 0, struct rr_devsel)
#define RR_DEVGET	 _IOR(__RR_IOC_MAGIC, 1, struct rr_devsel)
#define RR_READ		_IOWR(__RR_IOC_MAGIC, 2, struct rr_iocmd)
#define RR_WRITE	 _IOW(__RR_IOC_MAGIC, 3, struct rr_iocmd)

#define VFAT_IOCTL_READDIR_BOTH         _IOR('r', 1, struct dirent [2])


#endif /* __RAWRABBIT_H__ */
