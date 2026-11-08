#!/bin/bash
qemu-system-i386 -drive file=bin/os_image.bin,format=raw,index=0,media=disk
