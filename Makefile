
DIRS = kernel user bench doc

all:
	@for d in $(DIRS); do $(MAKE) -C $$d $@ || exit 1; done

clean:
	@for d in $(DIRS); do $(MAKE) -C $$d $@ || exit 1; done
