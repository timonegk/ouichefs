#!/bin/bash

. /share/tests/color.sh

cr "echo 1 > /mnt/test2"
cr "cat /mnt/test2"
cr "echo 2 > /mnt/test2"
cr "cat /mnt/test2"
cr "cat /share/tests/msg.txt >> /mnt/test2"

cr "cat /sys/kernel/debug/loop0"
# Il y aura quatre versions
cr "echo 3 > /mnt/test3"
cr "cat /sys/kernel/debug/loop0"
