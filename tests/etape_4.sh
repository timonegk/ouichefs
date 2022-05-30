#!/bin/bash

. /share/tests/color.sh

cr "echo 1 > /mnt/test5"
cr "echo 2 > /mnt/test5"
cr "cat /mnt/test5"

cr "/share/client version /mnt/test5 1"
cr "cat /mnt/test5"

cr "cat /sys/kernel/debug/loop0"

cr "/share/client reset /mnt/test5"
cr "cat /mnt/test5"

cr "cat /sys/kernel/debug/loop0"

# On peut écrire
cr "echo 3 > /mnt/test5"