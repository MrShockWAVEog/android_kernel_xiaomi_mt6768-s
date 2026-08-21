#!/bin/bash

mkdir -p rootfs
aarch64-linux-gnu-gcc -static -O2 ./init.c -o ./rootfs/init

chmod +x ./rootfs/init

cd ./rootfs
find . -print0 | cpio --null --create --format=newc > ../initramfs.cpio