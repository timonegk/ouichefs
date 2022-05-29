#!/bin/bash

echo 1 > /mnt/test2
cat /mnt/test2
echo 2 > /mnt/test2
cat /mnt/test2
cat /share/tests/msg.txt >> /mnt/test2

# Il y aura quatre versions
cat /sys/kernel/debug/loop0

echo 3 > /mnt/test3
cat /sys/kernel/debug/loop0
