#!/bin/bash
DISK_COUNT=2
DISK_SIZE_MB=1

while getopts "n:s:" opt; do
  case $opt in
    n) DISK_COUNT=$OPTARG ;;
    s) DISK_SIZE_MB=$OPTARG ;; 
    *) echo "Usage: $0 [-n <number_of_disks>] [-s <size_of_each_disk_in_MB>]"
       exit 1 ;;
  esac
done

for i in $(seq 1 $DISK_COUNT); do
  DISK_NAME="disk$i.img"
  echo "Creating $DISK_NAME of size ${DISK_SIZE_MB}MB..."
  dd if=/dev/zero of=$DISK_NAME bs=1M count=$DISK_SIZE_MB
done

echo "All disks created successfully."