# FUSE_filesystem

## Overview

FUSE_filesystem is a custom file system implementation built on top of [FUSE (Filesystem in Userspace)](https://github.com/libfuse/libfuse). It supports multiple RAID modes (RAID0, RAID1, and RAID1V), providing redundancy and/or performance improvements across multiple disk files. The project includes tools for formatting disks (`mkfs`) and mounting the filesystem (`wfs`). The layout and behavior are tailored for educational purposes and closely follow a typical UNIX filesystem structure, with custom superblock, inode, and data management.

## Project Structure

```
FUSE_filesystem/
│
├── disk-layout.pdf          # Filesystem disk layout documentation (PDF)
├── disk-layout.svg          # Filesystem disk layout diagram (SVG)
├── src/
│   ├── wfs.c                # Main FUSE operations and FS logic
│   ├── wfs.h                # FS data structures and constants
│   ├── mkfs.c               # File system formatter: creates/initializes disks
│   ├── Makefile             # Build script for mkfs and wfs
│   └── README.md            # (Placeholder)
└── tests/
    └── wfs-check-metadata.py # Metadata and RAID mode verification scripts
```

## Features

- **Custom File System:** Implements basic file and directory operations using FUSE.
- **RAID Support:** Supports RAID0 (striping), RAID1 (mirroring), and RAID1V (mirroring with verification) modes for data redundancy and performance.
- **Disk Layout:** Custom superblock, inode table, data block management, and bitmaps for inodes/data.
- **Multiple Disk Support:** Operates over multiple disk files, simulating physical disks.
- **Debug Utilities:** Includes tools to print and debug bitmap states and inodes.

## Disk Layout

See `disk-layout.pdf` or `disk-layout.svg` for visual diagrams of the on-disk structure.

Key regions:
- **Superblock:** Filesystem metadata.
- **Bitmaps:** Track allocation of inodes and data blocks.
- **Inode Table:** Stores file and directory metadata.
- **Data Blocks:** Store actual file contents.

## Build Instructions

### Prerequisites

- GCC (C compiler)
- [FUSE library (libfuse)](https://github.com/libfuse/libfuse)
- Python 3 (for running test scripts)

### Building

```bash
cd src
make
```

This produces two binaries in `src/`:
- `mkfs` — Filesystem formatter
- `wfs`  — FUSE filesystem daemon

## Usage

### 1. Formatting Disks

Create at least two disk files (required for any RAID mode):

```bash
dd if=/dev/zero of=disk1.img bs=1M count=10
dd if=/dev/zero of=disk2.img bs=1M count=10
```

Format disks with `mkfs`:

```bash
./mkfs -d disk1.img -d disk2.img -i <num_inodes> -b <num_blocks> -r <raid_mode>
```

- `-d <disk_file>`: Specify a disk image file (repeat for each disk)
- `-i <num_inodes>`: Number of inodes
- `-b <num_blocks>`: Number of data blocks per disk
- `-r <raid_mode>`: RAID mode (`0` for RAID0, `1` for RAID1, `1v` for RAID1V)

Example:

```bash
./mkfs -d disk1.img -d disk2.img -i 128 -b 1024 -r 1
```

### 2. Mounting the Filesystem

Create a mount point and mount using `wfs`:

```bash
mkdir ~/mnt/fusefs
./wfs disk1.img disk2.img -f ~/mnt/fusefs
```

- The program expects at least two disk files as arguments, followed by FUSE options and the mount point.

### 3. Interacting with the File System

Standard file operations (via shell, scripts, or programs) are supported on the mounted directory (`~/mnt/fusefs`).

### 4. Running Tests

Use the provided Python script to check the correctness of the file system:

```bash
cd tests
python3 wfs-check-metadata.py --mode <mode> --disks disk1.img disk2.img ...
```

See script help for more options.

## Implementation Notes

- **Minimum Disks:** At least two disk files are required (`MIN_DISKS = 2`).
- **Block Size:** Fixed at 512 bytes.
- **RAID Modes:**
  - **RAID0:** Data is striped across disks; no redundancy.
  - **RAID1:** Data is mirrored; each disk contains a full copy.
  - **RAID1V:** Adds verification for mirrored data.
- **Superblock and Metadata:** Only data blocks participate in RAID; inodes and metadata are not striped/mirrored.
- **Safety:** Always unmount and backup disk images before changing RAID modes or modifying low-level parameters.

## Acknowledgments

- Built on top of the FUSE library.
- Inspired by UNIX file system principles and academic filesystem projects.

---

**Author:** [luisylizaliturri](https://github.com/luisylizaliturri)
