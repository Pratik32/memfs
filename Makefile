obj-m+=memfs.o
ccflags-y := -I$(src)/.
all:
	make -C /lib/modules/`uname -r`/build M=`pwd` modules
clean:
	make -C /lib/modules/`uname -r`/build M=`pwd` clean

