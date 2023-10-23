TARGET_KERNEL ?= ~/dev/EmuELEC/build.EmuELEC-Amlogic.aarch64-4.3/linux-07d26b4ce91cf934d65a64e2da7ab3bc75e59fcc
TARGET_CROSS_COMPILE ?= ~/dev/EmuELEC/build.EmuELEC-Amlogic.aarch64-4.3/toolchain/bin/aarch64-libreelec-linux-gnueabi-
TARGET_ARCH ?= arm64
OUTPUT_FOLDER ?= pandoraclone

GCCBIN ?= ${TARGET_CROSS_COMPILE}gcc
EXTRA_CFLAGS += -I${TARGET_KERNEL}/include 
obj-m += pandoraclone.o serio.o

all: inputattach module
	
module: export ARCH = ${TARGET_ARCH}
module: export CROSS_COMPILE = ${TARGET_CROSS_COMPILE}
module:
	make -C ${TARGET_KERNEL} M=$(PWD) modules

inputattach:
	${GCCBIN} -o inputattach-pandora inputattach.c

install: all
	cp pandoraclone.ko ${OUTPUT_FOLDER}
	cp inputattach-pandora ${OUTPUT_FOLDER}

clean: export ARCH = ${TARGET_ARCH}
clean: export CROSS_COMPILE = ${TARGET_CROSS_COMPILE}
clean:
	make -C ${TARGET_KERNEL} M=$(PWD) clean
	rm -rf ${OUTPUT_FOLDER}/inputattach-pandora
	rm -rf ${OUTPUT_FOLDER}/*.ko
