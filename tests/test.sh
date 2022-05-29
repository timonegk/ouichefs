#!/bin/bash

echo ">>> Mounting system"
/share/tests/mount.sh
echo ">>> Testing etape 1"
/share/tests/etape_1.sh
echo ">>> Testing etape 2"
/share/tests/etape_2.sh
echo ">>> Testing etape 3"
/share/tests/etape_3.sh
echo ">>> Testing etape 4"
/share/tests/etape_4.sh
echo ">>> Testing etape 5"
/share/tests/etape_5.sh
