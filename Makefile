
DIRS = kernel user doc

all:
	@for d in $(DIRS); do $(MAKE) -C $$d $@ || exit 1; done

clean:
	@for d in $(DIRS); do $(MAKE) -C $$d $@ || exit 1; done