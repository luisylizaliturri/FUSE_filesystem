#!/bin/bash

# cleanup.sh - Clean up WFS mount points and temporary files

# Exit on any error
set -e

echo "Cleaning up WFS files and mount points..."

# Clean up temp files
if [ -d "/tmp/$(whoami)" ]; then
    rm -rf "/tmp/$(whoami)/"*
fi

fusermount -u mnt || true
rm -f test_output.log || true

# Clean up mount directory
if [ -d "mnt" ]; then
    rm -rf mnt/*
    rmdir mnt
fi

echo "Cleanup completed successfully"