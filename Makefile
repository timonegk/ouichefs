obj-m += ouichefs.o
ouichefs-objs := fs.o super.o inode.o file.o dir.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build

all: client
	make -C $(KERNELDIR) M=$(PWD) modules
	cp ouichefs.ko ../share
	cp client ../share

client: ioctl_client.c
	gcc -o client ioctl_client.c

debug:
	make -C $(KERNELDIR) M=$(PWD) ccflags-y+="-DDEBUG -g" modules

clean:
	make -C $(KERNELDIR) M=$(PWD) clean
	rm -rf *~

.PHONY: all clean
