#!/bin/bash

. /share/tests/color.sh

echo -e "\033[1m>>> Creating partition\033[0m"
/share/tests/recreate.sh
echo -e "\033[1m>>> Mounting system\033[0m"
/share/tests/mount.sh
echo -e "\n\n\033[1m>>> Testing etape 1\033[0m"
/share/tests/etape_1.sh
echo -e "\n\n\033[1m>>> Testing etape 2\033[0m"
/share/tests/etape_2.sh
echo -e "\n\n\033[1m>>> Testing etape 3\033[0m"
/share/tests/etape_3.sh
echo -e "\n\n\033[1m>>> Testing etape 4\033[0m"
/share/tests/etape_4.sh
echo -e "\n\n\033[1m>>> Testing etape 5\033[0m"
/share/tests/etape_5.sh
