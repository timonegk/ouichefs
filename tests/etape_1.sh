#!/bin/bash

. /share/tests/color.sh

cr "echo 1 > /mnt/test1"
cr "cat /mnt/test1"
cr "echo 2 > /mnt/test1"
cr "cat /mnt/test1"
