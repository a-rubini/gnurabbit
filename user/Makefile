
CFLAGS = -Wall -ggdb -I../kernel

AS		= $(CROSS_COMPILE)as
LD		= $(CROSS_COMPILE)ld
CC		= $(CROSS_COMPILE)gcc
CPP		= $(CC) -E
AR		= $(CROSS_COMPILE)ar
NM		= $(CROSS_COMPILE)nm
STRIP		= $(CROSS_COMPILE)strip
OBJCOPY		= $(CROSS_COMPILE)objcopy
OBJDUMP		= $(CROSS_COMPILE)objdump

ALL = rrcmd flip loadfile

all: $(ALL)

loadfile: load-main.o loader-ll.o
	$(CC) $^ $(LDFLAGS) -o $@

loader-ll.o: ../kernel/loader-ll.c
	$(CC) $(CFLAGS) $^ -c -o $@

clean:
	rm -f $(ALL) *.o *~
