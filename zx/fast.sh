#!/bin/bash
#
#

TAG=$(date "+%Y-%m-%d-%H%M%S")

make clean
make

sed -i '/DRIVER_VERSION/d' zx_i2s.h
echo "#define DRIVER_VERSION  \"debug-$TAG\"" >> zx_i2s.h

#git add .
#git commit -m $1
#git push

