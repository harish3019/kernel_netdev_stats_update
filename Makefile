#If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.
ifneq ($(KERNELRELEASE),)
	obj-m := tun_module.o
# Otherwise we were called directly from the command
# line; invoke the kernel build system.
else

	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules 

clean:
	rm -rf tun_module.o tun_module.mod.o tun_module.ko tun_module.mod.c Module.symvers modules.order .tun_module.ko.cmd .tun_module.mod.o.cmd .tun_module.o.cmd .tmp_versions

endif
