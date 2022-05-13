# SPDX-License-Identifier: GPL-2.0-or-later
# Zhaoxin I2S Controller surpport

obj-m += pci_zx_i2s.o
obj-m += rt5645_zx_i2s.o
obj-m += dma_zx_i2s.o
obj-m += cpu_zx_i2s.o

PWD  := $(shell pwd)
KVER := $(shell uname -r)
KDIR := /lib/modules/$(KVER)/build

all:
	$(MAKE) -C $(KDIR) -Wno-implicit-fallthrough  M=$(PWD) modules
	rm -rf .*.cmd *.o *.mod.c .tmp_versions/ modules.* Module.* *~  *.mod .cache.mk

clean:

install:
	
uninstall:
