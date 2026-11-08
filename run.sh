#!/bin/bash
qemu-system-i386 -drive file=bin/os_image.bin,format=raw,bus=0,unit=0,media=disk
