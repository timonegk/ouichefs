#!/bin/bash

. /share/tests/color.sh

cr "umount /mnt"
cr "dd if=/dev/zero of=test.img bs=1M count=30"
cr "/share/mkfs.ouichefs test.img"
