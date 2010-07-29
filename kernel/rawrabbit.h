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

#ifdef __KERNEL__ /* The rest of the file is for driver-internal stuff */
#include <linux/pci.h>

struct rr_dev {
	struct pci_driver	*pci_driver;
	struct pci_device_id	*id_table;
	spinlock_t		 lock;
	struct rr_devsel	 devsel;
	int			 usecount;
	int			 devcount;
	int			 proberesult;
};

#endif /* __KERNEL__ */
#endif /* __RAWRABBIT_H__ */
