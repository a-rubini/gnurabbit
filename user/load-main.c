
/* Trivial frontend for the gennum loader (../kernel/loader-ll.c) */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "rawrabbit.h"
#include "loader-ll.h"

#define DEVNAME "/dev/rawrabbit"

static char buf[64*1024*1024]; /* 64 MB binary? */

int main(int argc, char **argv)
{
	int fd;
	FILE *f;
	int nbytes, rval;

	if (argc != 2) {
		fprintf(stderr, "%s: Use \"%s <firmware-file>\n",
			argv[0], argv[0]);
		exit(1);
	}

	f = fopen(argv[1], "r");
	if (!f) {
		fprintf(stderr, "%s: %s: %s\n",
			argv[0], argv[1], strerror(errno));
		exit(1);
	}

	fd = open(DEVNAME, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "%s: %s: %s\n",
			argv[0], DEVNAME, strerror(errno));
		exit(1);
	}
	nbytes = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	if (nbytes < 0) {
		fprintf(stderr, "%s: %s: %s\n",
			argv[0], argv[1], strerror(errno));
		exit(1);
	}
	printf("Programming %i bytes of binary gateware\n", nbytes);

	rval = loader_low_level(fd, NULL, buf, nbytes);
	if (rval < 0) {
		fprintf(stderr, "%s: load_firmware: %s\n",
			argv[0], strerror(-rval));
		exit(1);
	}
	/* We must now wait for the "done" interrupt bit */
	{
		unsigned long t = time(NULL) + 3;
		int i, done = 0;
		struct rr_iocmd iocmd = {
			.datasize = 4,
			.address = FCL_IRQ | __RR_SET_BAR(4),
		};

	if (ioctl(fd, RR_READ, &iocmd) < 0) perror("ioctl");

		while (time(NULL) < t) {
			if (ioctl(fd, RR_READ, &iocmd) < 0) perror("ioctl");
			i = iocmd.data32;
			if (i & 0x8) {
				done = 1;
				break;
			}
			if (i & 0x4) {
				fprintf(stderr,"Error after %i words\n", rval);
				exit(1);
			}
			usleep(100*1000);
		}
	}
	return 0;
}
