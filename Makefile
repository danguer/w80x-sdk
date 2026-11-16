# Build every example:  make            Clean:  make clean
EXAMPLES := $(patsubst %/Makefile,%,$(wildcard examples/*/Makefile))

.PHONY: all clean $(EXAMPLES)
all: $(EXAMPLES)

$(EXAMPLES):
	$(MAKE) -C $@

clean:
	@for d in $(EXAMPLES); do $(MAKE) -C $$d clean; done
