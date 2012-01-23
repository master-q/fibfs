#
# Makefile for the linux fibfs routines.
#
.PHONY : clean install clean modprobe mount umount

KERNEL_VERSION	:= `uname -r`
KERNEL_DIR	:= /lib/modules/$(KERNEL_VERSION)/build

PWD		:= $(shell pwd)

obj-m		:= fibfs.o

all: fibfs.ko
fibfs.ko: fibfs.c
	@echo "Building fibfs filesystem..."
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules
install:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules_install
	depmod -ae
clean:
	rm -f *~
	rm -f Module.symvers Module.markers modules.order
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean

modprobe: fibfs
	chmod a+r fibfs.ko
	-sudo rmmod $<
	sudo insmod ./fibfs.ko

mount:
	mkdir -p mount
	sudo mount -t fibfs /dev/null $(PWD)/mount
umount:
	sudo umount $(PWD)/mount
	rm -rf mount
