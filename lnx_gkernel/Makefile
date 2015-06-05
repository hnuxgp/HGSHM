obj-m+=hgshm.o

kernel_version=3.2.0-23-generic
kernel_version=3.2.0-29-generic

all: driver

driver:
	make -C /lib/modules/${kernel_version}/build M=$(PWD) modules

clean:
	make -C /lib/modules/${kernel_version}/build M=$(PWD) clean
	rm -f Module.markers  Module.symvers
