#!/bin/bash

echo 1 > /mnt/test5
echo 2 > /mnt/test5
cat /mnt/test5  # 2

/share/client version /mnt/test5 1
cat /mnt/test5  # 1

cat /sys/kernel/debug/loop0

/share/client reset /mnt/test5
cat /mnt/test5  # 1

cat /sys/kernel/debug/loop0

# On peut écrire
echo 3 > /mnt/test5