#!/bin/bash

. /share/tests/color.sh

cr "echo 1 > /mnt/test4"

cr "echo 2 > /mnt/test4"

cr "cat /mnt/test4"

cr "/share/client version /mnt/test4 1"
cr "cat /mnt/test4"

cr "/share/client version /mnt/test4 0"
cr "cat /mnt/test4"

cr "/share/client version /mnt/test4 1"
cr "echo 3 > /mnt/test4"


cr "/share/client version /mnt/test4 0"
cr "rm /mnt/test4"
