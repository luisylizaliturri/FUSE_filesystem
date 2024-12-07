#define FUSE_USE_VERSION 30

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fuse.h>
#include "wfs.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <limits.h>
#include <sys/types.h>

// Global variables
int raid_mode = -1;
char **disk_files = NULL;
size_t num_disks = 0;
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

//Helpers
void split_path(const char *path, char *parent_path, char *dir_name) {
    char *last_slash = strrchr(path, '/');
    if (!last_slash || last_slash == path) {
        strcpy(parent_path, "/");
        strcpy(dir_name, last_slash ? last_slash + 1 : path);
    } else {
        strncpy(parent_path, path, last_slash - path);
        parent_path[last_slash - path] = '\0';
        strcpy(dir_name, last_slash + 1);
    }
}

int parent_inode_num(int current_inode_num) {
    // Locate the inode table
    struct wfs_inode *inode_table = (struct wfs_inode *)((char *)disk_maps[0] + superblock.i_blocks_ptr);

    // Retrieve the current inode
    struct wfs_inode *current_inode = &inode_table[current_inode_num];

    // If the current inode is the root inode, return itself
    if (current_inode_num == 0) {
        return 0; // Root directory points to itself
    }

    // Retrieve the data block of the current inode
    struct wfs_dentry *dir_entries = (struct wfs_dentry *)((char *)disk_maps[0] + superblock.d_blocks_ptr + current_inode->blocks[0] * BLOCK_SIZE);

    // Iterate through directory entries to find ".."
    for (int i = 0; i < BLOCK_SIZE / sizeof(struct wfs_dentry); i++) {
        if (strcmp(dir_entries[i].name, "..") == 0) {
            return dir_entries[i].num; // Parent inode number
        }
    }

    // If ".." is not found, return an error
    fprintf(stderr, "Error: Parent directory entry not found for inode %d.\n", current_inode_num);
    return -ENOENT;
}

int find_dir_entry(struct wfs_inode *parent_inode, const char *dir_name) {
    // Get the parent directory's data block pointer
    struct wfs_dentry *dir_entries = (struct wfs_dentry *)((char *)disk_maps[0] + superblock.d_blocks_ptr + parent_inode->blocks[0] * BLOCK_SIZE);
    // Iterate through the directory entries in the parent
    for (int i = 0; i < BLOCK_SIZE / sizeof(struct wfs_dentry); i++) {
        if (dir_entries[i].num != 0 && strcmp(dir_entries[i].name, dir_name) == 0) {
            // Entry found, return the inode number
            return dir_entries[i].num;
        }
    }
    return -ENOENT;// Entry not found
}

int find_inode_by_path(const char *path) {
    if (strcmp(path, "/") == 0) {
        return 0; // Root inode
    }
    char parent_path[PATH_MAX], dir_name[MAX_NAME];
    split_path(path, parent_path, dir_name);

    // Handle "." and ".."
    if (strcmp(dir_name, ".") == 0) {
        return find_inode_by_path(parent_path); // Stay in the current directory
    }
    if (strcmp(dir_name, "..") == 0) {
        // Special handling for ".." (return parent's inode number)
        // This assumes a parent_inode_num function exists
        return parent_inode_num(find_inode_by_path(parent_path));
    }

    int parent_inode_num = find_inode_by_path(parent_path);
    if (parent_inode_num < 0) {
        return -ENOENT; // Parent not found
    }
    struct wfs_inode *parent_inode = &((struct wfs_inode *)((char *)disk_maps[0] + superblock.i_blocks_ptr))[parent_inode_num];
    return find_dir_entry(parent_inode, dir_name); // Implemented below
}

int allocate_inode() {
    char *inode_bitmap = (char *)disk_maps[0] + superblock.i_bitmap_ptr;
    for (int i = 0; i < superblock.num_inodes; i++) {
        if (!(inode_bitmap[i / 8] & (1 << (i % 8)))) {
            inode_bitmap[i / 8] |= (1 << (i % 8)); // Mark as allocated
            return i;
        }
    }
    return -ENOSPC; // No free inodes
}

int add_dir_entry(struct wfs_inode *parent_inode, const char *name, int inode_num) {
    // Update the directory entry for all disks
    for (size_t disk = 0; disk < num_disks; disk++) {
        struct wfs_dentry *dir_entries = (struct wfs_dentry *)((char *)disk_maps[disk] + superblock.d_blocks_ptr + parent_inode->blocks[0] * BLOCK_SIZE);

        // Find a free entry in the directory block
        for (int i = 0; i < BLOCK_SIZE / sizeof(struct wfs_dentry); i++) {
            if (dir_entries[i].num == 0) { // Free entry
                strncpy(dir_entries[i].name, name, MAX_NAME);
                dir_entries[i].num = inode_num;
                parent_inode->size += sizeof(struct wfs_dentry);

                printf("Added directory entry: %s (inode %d) to disk %zu\n", name, inode_num, disk);
                break;
            }
        }
    }

    return 0;
}

int allocate_data_block() {
    char *data_bitmap = (char *)disk_maps[0] + superblock.d_bitmap_ptr;

    // Find a free block in the bitmap
    for (int i = 0; i < superblock.num_data_blocks; i++) {
        if (!(data_bitmap[i / 8] & (1 << (i % 8)))) {
            // Mark the block as allocated on all disks
            for (size_t disk = 0; disk < num_disks; disk++) {
                char *disk_data_bitmap = (char *)disk_maps[disk] + superblock.d_bitmap_ptr;
                disk_data_bitmap[i / 8] |= (1 << (i % 8));
            }
            return i; // Return the allocated block number
        }
    }

    return -ENOSPC; // No free data blocks
}









int wfs_getattr(const char *path, struct stat *stbuf) {
    printf("getattr called: %s\n", path);
    memset(stbuf, 0, sizeof(struct stat)); // Clear stat buffer
    // Check if the path is the root directory
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755; 
        stbuf->st_nlink = 2;             // "." and ".."
        stbuf->st_uid = getuid();
        stbuf->st_gid = getgid();
        return 0;
    }
    // Locate the inode for the given path
    int inode_num = find_inode_by_path(path);
    if (inode_num < 0) {
        return -ENOENT; // Path not found
    }
    // Retrieve the inode and populate stbuf
    struct wfs_inode *inode_table = (struct wfs_inode *)((char *)disk_maps[0] + superblock.i_blocks_ptr);
    struct wfs_inode *inode = &inode_table[inode_num];
    stbuf->st_mode = inode->mode;        // File type and permissions
    stbuf->st_nlink = inode->nlinks;    // Number of links
    stbuf->st_uid = inode->uid;         // User ID
    stbuf->st_gid = inode->gid;         // Group ID
    stbuf->st_size = inode->size;       // Size in bytes
    stbuf->st_atime = inode->atim;      // Access time
    stbuf->st_mtime = inode->mtim;      // Modification time
    stbuf->st_ctime = inode->ctim;      // Status change time
    printf("getattr success: inode %d\n", inode_num);
    return 0;
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

    // Parse the path into parent and directory name
    char parent_path[PATH_MAX];
    char dir_name[MAX_NAME];
    split_path(path, parent_path, dir_name);

    struct wfs_inode *inode_table = (struct wfs_inode *)((char *)disk_maps[0] + superblock.i_blocks_ptr);
    int parent_inode_num = find_inode_by_path(parent_path);
    if (parent_inode_num < 0) {
        fprintf(stderr, "Error: Parent directory does not exist.\n");
        return -ENOENT;
    }

    struct wfs_inode *parent_inode = &inode_table[parent_inode_num];
    if (!(parent_inode->mode & S_IFDIR)) {
        fprintf(stderr, "Error: Parent is not a directory.\n");
        return -ENOTDIR;
    }

    // Allocate a data block for the parent directory
    if (parent_inode->blocks[0] == 0) {
        int parent_block_num = allocate_data_block();
        if (parent_block_num < 0) {
            fprintf(stderr, "Error: No free data blocks available for parent directory.\n");
            return -ENOSPC;
        }
        parent_inode->blocks[0] = parent_block_num;
    }

    // Allocate a new inode for the new directory
    int new_inode_num = allocate_inode();  // Declare and initialize here
    if (new_inode_num < 0) {
        fprintf(stderr, "Error: No free inodes available.\n");
        return -ENOSPC;
    }

    // Add the new directory entry to the parent directory
    if (add_dir_entry(parent_inode, dir_name, new_inode_num) < 0) {
        fprintf(stderr, "Error: Failed to add directory entry to parent.\n");
        return -ENOSPC;
    }

    // Initialize the new inode
    struct wfs_inode new_inode = {0};
    new_inode.num = new_inode_num;
    new_inode.mode = S_IFDIR | mode;
    new_inode.uid = getuid();
    new_inode.gid = getgid();
    new_inode.size = 0;
    new_inode.nlinks = 2; // "." and ".."
    new_inode.atim = time(NULL);
    new_inode.mtim = time(NULL);
    new_inode.ctim = time(NULL);

    // Update the inode table and bitmap on all disks
    for (size_t i = 0; i < num_disks; i++) {
        char *inode_bitmap = (char *)disk_maps[i] + superblock.i_bitmap_ptr;
        inode_bitmap[new_inode_num / 8] |= (1 << (new_inode_num % 8));

        struct wfs_inode *disk_inode_table = (struct wfs_inode *)((char *)disk_maps[i] + superblock.i_blocks_ptr);
        memcpy(&disk_inode_table[new_inode_num], &new_inode, sizeof(struct wfs_inode));
    }

    printf("Directory '%s' created successfully with inode %d.\n", dir_name, new_inode_num);
    return 0;
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







// void initialize_root_directory() {
//     // Check if the root inode is already initialized
//     char *inode_bitmap = (char *)disk_maps[0] + superblock.i_bitmap_ptr;
//     if ((inode_bitmap[0] & 1) == 0) { // Check if the first bit is not set
//         fprintf(stderr, "Error: Root inode is not initialized. Filesystem may be corrupted.\n");
//         exit(EXIT_FAILURE);
//     }
//     printf("Root directory inode initialized.\n");
// }

void initialize_root_directory() {
    printf("Initializing root directory...\n");

    // Locate the inode bitmap
    char *inode_bitmap = (char *)disk_maps[0] + superblock.i_bitmap_ptr;

    // Check if the root inode is allocated
    if ((inode_bitmap[0] & 1) == 0) {
        fprintf(stderr, "Error: Root inode is not allocated. Filesystem may be corrupted.\n");
        exit(EXIT_FAILURE);
    }

    struct wfs_inode root_inode = {0};
    root_inode.num = 0;
    root_inode.mode = S_IFDIR | 0755;
    root_inode.uid = getuid();
    root_inode.gid = getgid();
    root_inode.size = 0; //BLOCK_SIZE; // Assume root directory takes 1 block
    root_inode.nlinks = 2;
    root_inode.atim = time(NULL);
    root_inode.mtim = time(NULL);
    root_inode.ctim = time(NULL);

    for (size_t i = 0; i < num_disks; i++) {
        // Update inode bitmap
        char *disk_inode_bitmap = (char *)disk_maps[i] + superblock.i_bitmap_ptr;
        disk_inode_bitmap[0] |= 1;

        // Write root inode to inode table
        struct wfs_inode *inode_table = (struct wfs_inode *)((char *)disk_maps[i] + superblock.i_blocks_ptr);
        memcpy(&inode_table[0], &root_inode, sizeof(struct wfs_inode));

        printf("Root inode written to disk %zu\n", i);
    }
}

// Function to initialize disks and load the superblock
void initialize_disks_and_superblock(int argc, char *argv[]) {
    printf("Initializing disks...\n");
    // Allocate memory for disk pointers
    disk_maps = malloc(num_disks * sizeof(void *));
    if (!disk_maps) {
        perror("Error allocating memory for disk mappings");
        exit(EXIT_FAILURE);
    }
    // Open and map each disk file
    for (size_t i = 0; i < num_disks; i++) {
        int fd = open(disk_files[i], O_RDWR);
        if (fd == -1) {
            perror("Error opening disk file");
            exit(EXIT_FAILURE);
        }
        struct stat st;
        if (fstat(fd, &st) == -1) {
            perror("Error getting disk file size");
            exit(EXIT_FAILURE);
        }
        disk_maps[i] = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (disk_maps[i] == MAP_FAILED) {
            perror("Error mapping disk file");
            exit(EXIT_FAILURE);
        }
        close(fd);
    }
    // Load the superblock from the first disk
    memcpy(&superblock, disk_maps[0], sizeof(struct wfs_sb));

    // Validate the superblock
    if (superblock.num_inodes == 0 || superblock.num_data_blocks == 0) {
        fprintf(stderr, "Error: Invalid superblock. Filesystem not initialized.\n");
        exit(EXIT_FAILURE);
    }
    // DEBUG
    // printf("Superblock (wfs):\n");
    // printf("  Inode bitmap offset: %zu\n", superblock.i_bitmap_ptr);
    // printf("  Data bitmap offset: %zu\n", superblock.d_bitmap_ptr);
    // printf("  Inode region offset: %zu\n", superblock.i_blocks_ptr);
    // printf("  Data blocks offset: %zu\n", superblock.d_blocks_ptr);
    printf("Superblock loaded successfully.\n");
    printf("Number of inodes: %zu, Number of data blocks: %zu\n",
           superblock.num_inodes, superblock.num_data_blocks);
    printf("Initializing disks complete.\n");
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
}

// Main function
int main(int argc, char *argv[]) {
    parse_args(argc, argv);
    initialize_disks_and_superblock(argc, argv);
    initialize_root_directory();
    //Debug
    // printf("Number of disks: %ld\n", num_disks);
    // printf("list of arguments");
    // for (int i = num_disks+ 1; i < argc; i++) {
    //     printf("%s ", argv[i]);
    // }
    printf("WFS starting...\n");
    return fuse_main(argc-(num_disks + 1), &argv[num_disks+ 1], &ops, NULL);
}