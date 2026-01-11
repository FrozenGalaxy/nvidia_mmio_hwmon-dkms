obj-m := nvidia_mmio_hwmon.o

KDIR ?= /lib/modules/$(KERNELRELEASE)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
