# obj-m ,obj-y are built in object goals of linux's kbuild system.
# obj-m specifies object files that are built as loadable kernel modules.
# obj-y specifies object files that are included as part of linux kernel
# <module-name>-objs is a list of object files that are included as part of 
# loadable kernel module.

obj-m += memfs.o
ccflags-y := -I$(src)/.
ifeq (${DBG},yes)
ccflags-y += -DDBG=${DBG}
endif
all:
	make -C /lib/modules/`uname -r`/build M=`pwd` modules
clean:
	make -C /lib/modules/`uname -r`/build M=`pwd` clean

