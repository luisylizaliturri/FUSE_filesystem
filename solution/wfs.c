#define FUSE_USE_VERSION 30

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fuse.h>
#include "wfs.h"

// Global variables
int raid_mode = -1;
char **disk_files = NULL;
size_t num_disks = 0;

// Global superblock
struct wfs_sb superblock;

// Disk pointers
void **disk_maps = NULL;

//Prototypes
int wfs_getattr(const char *path, struct stat *stbuf);
int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
int wfs_mknod(const char *path, mode_t mode, dev_t rdev);
int wfs_mkdir(const char *path, mode_t mode);
int wfs_unlink(const char *path);
int wfs_rmdir(const char *path);
int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);


// // Define FUSE operations
static struct fuse_operations ops = {
  .getattr = wfs_getattr,
  .mknod   = wfs_mknod,
  .mkdir   = wfs_mkdir,
  .unlink  = wfs_unlink,
  .rmdir   = wfs_rmdir,
  .read    = wfs_read,
  .write   = wfs_write,
  .readdir = wfs_readdir,
};

// FUSE operation placeholders
int wfs_getattr(const char *path, struct stat *stbuf) {
    printf("getattr called: %s\n", path);
    memset(stbuf, 0, sizeof(struct stat));
    return -ENOENT; // Not implemented yet
}

int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    printf("readdir called: %s\n", path);
    return 0; // Not implemented yet
}

int wfs_mknod(const char *path, mode_t mode, dev_t rdev) {
    printf("mknod called: %s\n", path);
    return 0; // Not implemented yet
}

int wfs_mkdir(const char *path, mode_t mode) {
    printf("mkdir called: %s\n", path);
    return 0; // Not implemented yet
}

int wfs_unlink(const char *path) {
    printf("unlink called: %s\n", path);
    return 0; // Not implemented yet
}

int wfs_rmdir(const char *path) {
    printf("rmdir called: %s\n", path);
    return 0; // Not implemented yet
}

int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    printf("read called: %s\n", path);
    return 0; // Not implemented yet
}

int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    printf("write called: %s\n", path);
    return 0; // Not implemented yet
}

// Function to initialize disks and load the superblock
void initialize_disks_and_superblock(int argc, char *argv[]) {
    printf("Initializing disks...\n");

    // Ensure sufficient arguments are passed
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <mountpoint>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Example: Map disks and read the superblock (to be implemented)
    // Disk pointers and superblock should be loaded here

    printf("Disks initialized.\n");
}








void parse_args(int argc, char *argv[]) {
    if (argc < 3) { // ./wfs disk1 disk2 mount_point
        fprintf(stderr, "Usage: %s <disk1> <disk2> [FUSE options] <mount_point>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Store disk files
    size_t i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "-f") == 0) {
            break; // Stop at FUSE options
        }
        disk_files = realloc(disk_files, (num_disks + 1) * sizeof(char *));
        if (!disk_files) {
            perror("Error allocating memory for disk files");
            exit(EXIT_FAILURE);
        }
        disk_files[num_disks++] = argv[i];
    }

    // Validate the number of disk files
    if (num_disks < 2) {
        fprintf(stderr, "Error: At least two disk files are required.\n");
        exit(EXIT_FAILURE);
    }

    // Validate mount point
    if (i >= argc) {
        fprintf(stderr, "Error: Mount point not specified.\n");
        exit(EXIT_FAILURE);
    }
    //const char *mount_point = argv[argc - 1]; // Last argument is the mount point

    //DEBUG
    // printf("Number of disks: %zu\n", num_disks);
    // for (size_t j = 0; j < num_disks; j++) {
    //     printf("Disk %zu: %s\n", j + 1, disk_files[j]);
    // }
    // printf("Mount point: %s\n", mount_point);
}

// Main function
int main(int argc, char *argv[]) {
    parse_args(argc, argv);
    initialize_disks_and_superblock(argc, argv);

    //Debug
    // printf("Number of disks: %ld\n", num_disks);
    // printf("list of arguments");
    // for (int i = num_disks+ 1; i < argc; i++) {
    //     printf("%s ", argv[i]);
    // }


    printf("WFS starting...\n");
    return fuse_main(argc-(num_disks + 1), &argv[num_disks+ 1], &ops, NULL);
}