obj-m+=memfs.o

clean:
	rm *.o
	rm *.ko
	rm *.mod.c
	rm Module.symvers
	rm modules.order
