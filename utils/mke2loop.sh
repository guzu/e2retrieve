#!/bin/sh

if [ $# -lt 3 ]; then
  echo "Too few arguments"
  echo "Usage: mke2loop.sh <FILE> <SIZE in megaoctet> <BLOCSIZE>"
  exit
fi


dd if=/dev/zero of=$1 bs=1M count=$2
/sbin/mke2fs -F $1

