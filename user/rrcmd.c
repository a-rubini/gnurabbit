/*
 * User space frontend (command line) for the raw I/O interface
 *
 * Copyright (C) 2010 Alessandro Rubini <rubini@gnudd.com>
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project and has been sponsored
 * by CERN, the European Institute for Nuclear Research.
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "rawrabbit.h"

#define DEVNAME "/dev/rawrabbit"

char *prgname;

void help(void)
{
	fprintf(stderr, "%s: use like this (all numbers are hex):\n"
		"   %s [<vendor:device>[/<subvendor>:<subdev>]"
		"[@<bus>:<devfn>]] <cmd>\n", prgname, prgname);
	fprintf(stderr, "   <cmd> = info\n");
	fprintf(stderr, "   <cmd> = r[<sz>] <bar>:<addr>\n");
	fprintf(stderr, "   <cmd> = w[<sz>] <bar>:<addr> <val>\n");
	fprintf(stderr, "      <sz> = 1, 2, 4, 8 (default = 4)\n");
	fprintf(stderr, "      <bar> = 0, 2, 4\n");
	exit(1);
}

int parse_devsel(int fd, char *arg)
{
	struct rr_devsel devsel;
	int n;

	devsel.subvendor = RR_DEVSEL_UNUSED;
	devsel.bus = RR_DEVSEL_UNUSED;

	if (strlen(arg) > 32) /* to prevent overflow with strtol */
		return -EINVAL;

	n = sscanf(arg, "%hx:%hx/%hx:%hx@%hx:%hx",
		   &devsel.vendor, &devsel.device,
		   &devsel.subvendor, &devsel.subdevice,
		   &devsel.bus, &devsel.devfn);
	switch(n) {
	case 6: /* all info */
	case 4: /* id/subid but no busdev */
		break;
	case 2:
		/* check if bus/dev is there */
		n = sscanf(arg, "%hx:%hx@%hx:%hx",
			   &devsel.vendor, &devsel.device,
			   &devsel.bus, &devsel.devfn);
		if (n == 4 || n == 2)
			break;
		/* fall through */
	default:
		printf("%s: can't parse \"%s\"\n", prgname, arg);
		return -EINVAL;
	}

	/* Now try to do the change */
	if (ioctl(fd, RR_DEVSEL, &devsel) < 0) {
		fprintf(stderr, "%s: %s: ioctl(DEVSEL): %s\n", prgname, DEVNAME,
			strerror(errno));
		return -EIO;
	}


	return 0;
}

int main(int argc, char **argv)
{
	struct rr_devsel devsel;
	struct rr_iocmd iocmd;
	int fd, ret = -EINVAL;

	prgname = argv[0];

	fd = open(DEVNAME, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "%s: %s: %s\n", prgname, DEVNAME,
			strerror(errno));
		exit(1);
	}

	/* parse argv[1] for devsel */
	if (argc > 1 && strchr(argv[1], ':')) {
		ret = parse_devsel(fd, argv[1]);
		if (!ret) {
			argc--, argv++;
		}
	}

	if (argc > 1 && !strcmp(argv[1], "info")) {
		if (ioctl(fd, RR_DEVGET, &devsel) < 0) {
			if (errno == ENODEV) {
				printf("%s: not bound\n", DEVNAME);
				exit(0);
			}
			fprintf(stderr, "%s: %s: ioctl(DEVGET): %s\n", prgname,
				DEVNAME, strerror(errno));
			exit(1);
		}
		printf("%s: bound to %04x:%04x/%04x:%04x@%04x:%04x\n", DEVNAME,
		       devsel.vendor, devsel.device,
		       devsel.subvendor, devsel.subdevice,
		       devsel.bus, devsel.devfn);
		ret = 0;
	}


	/* subroutines return "invalid argument" to ask for help */
	if (ret == -EINVAL)
		help();
	if (ret)
		exit(1);

	return 0;
}
