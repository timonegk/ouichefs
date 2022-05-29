#!/bin/bash

echo 1 > /mnt/test4
echo 2 > /mnt/test4
cat /mnt/test4  # 2

/share/client version /mnt/test4 1
cat /mnt/test4  # 1

/share/client version /mnt/test4 0
cat /mnt/test4  # 2

/share/client version /mnt/test4 1
echo 3 > /mnt/test4  # ERR


/share/client version /mnt/test4 0
rm /mnt/test4
