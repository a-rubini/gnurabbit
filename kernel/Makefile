
LINUX ?= /lib/modules/$(shell uname -r)/build

obj-m = rawrabbit.o
obj-m += spec-demo.o

rawrabbit-objs = rawrabbit-core.o loader.o loader-ll.o
spec-demo-objs = spec-demo-core.o loader.o loader-ll.o spec-loader.o

all modules:
	$(MAKE) -C $(LINUX) M=$(shell /bin/pwd) modules

clean:
	rm -rf *.o *~  .*.cmd *.ko *.mod.c .tmp_versions Module.symvers \
		Module.markers modules.order
