#!/bin/bash

qemu-system-aarch64 \
  -machine virt,gic-version=3 \
  -cpu cortex-a53 \
  -smp 1 \
  -m 1024 \
  -kernel ../arch/arm64/boot/Image \
  -initrd ./initramfs.cpio \
  -nographic \
  -append "console=ttyAMA0,115200 earlycon=pl011,0x09000000 keep_bootcon ignore_loglevel debug rdinit=/init"