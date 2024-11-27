#!/bin/bash
# Default values
DISK_COUNT=2
DISK_SIZE_MB=1

# Parse arguments
while getopts "n:s:" opt; do
  case $opt in
    n) DISK_COUNT=$OPTARG ;; # Number of disks
    s) DISK_SIZE_MB=$OPTARG ;; # Size of each disk in MB
    *) echo "Usage: $0 [-n <number_of_disks>] [-s <size_of_each_disk_in_MB>]"
       exit 1 ;;
  esac
done

# Create the specified number of disks
for i in $(seq 1 $DISK_COUNT); do
  DISK_NAME="disk$i.img"
  echo "Creating $DISK_NAME of size ${DISK_SIZE_MB}MB..."
  dd if=/dev/zero of=$DISK_NAME bs=1M count=$DISK_SIZE_MB
done

echo "All disks created successfully."