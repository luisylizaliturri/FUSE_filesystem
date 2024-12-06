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

// Main function
int main(int argc, char *argv[]) {
    initialize_disks_and_superblock(argc, argv);

    printf("WFS starting...\n");
    return fuse_main(argc, argv, &ops, NULL);
}