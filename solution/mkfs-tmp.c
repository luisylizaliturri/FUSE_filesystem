#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
//#include <sys/types.h>
#include "wfs.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>


#define MIN_DISKS 2
#define RAID0 0
#define RAID1 1
#define RAID1V 2

//globals
int raid_mode = -1;
char **disk_files = NULL;
size_t num_disks = 0;
size_t num_inodes = 0;
size_t num_data_blocks = 0;

struct wfs_sb sb;

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s -r <raid_mode> -d <disk_file> -i <num_inodes> -b <num_data_blocks>\n", prog_name);
    fprintf(stderr, "  <raid_mode>: 0 for RAID 0, 1 for RAID 1, 1v for RAID 1v\n");
    exit(EXIT_FAILURE);
}

//parse and validate CL arguments
void parse_args(int argc, char *argv[]) {
    if (argc < 7) {
        print_usage(argv[0]);
    }

    int opt;
    while ((opt = getopt(argc, argv, "r:d:i:b:")) != -1) {
        switch (opt) {
        case 'r':
            if (strcmp(optarg, "0") == 0) {
                raid_mode = RAID0;
            } else if (strcmp(optarg, "1") == 0) {
                raid_mode = RAID1;
            } else if (strcmp(optarg, "1v") == 0) {
                raid_mode = RAID1V;
            } else {
                fprintf(stderr, "Error: Invalid RAID mode.\n");
                print_usage(argv[0]);
            }
            break;
        case 'd':
            disk_files = realloc(disk_files, (num_disks + 1) * sizeof(char *));
            if (!disk_files) {
                perror("Error reallocating memory for disk files");
                exit(EXIT_FAILURE);
            }
            disk_files[num_disks++] = optarg;
            break;
        case 'i':
            num_inodes = atoi(optarg);
            if (num_inodes == 0) {
                fprintf(stderr, "Error: Invalid number of inodes.\n");
                print_usage(argv[0]);
            }
            break;
        case 'b':
            num_data_blocks = atoi(optarg);
            if (num_data_blocks == 0) {
                fprintf(stderr, "Error: Invalid number of data blocks.\n");
                print_usage(argv[0]);
            }
            break;
        default:
            print_usage(argv[0]);
        }
    }

    if (raid_mode == -1) {
        raid_mode = RAID0;
    }

    if (num_disks < MIN_DISKS) {
        fprintf(stderr, "Error: At least %d disk files are required.\n", MIN_DISKS);
        print_usage(argv[0]);
    }
    if (num_inodes == 0 || num_data_blocks == 0) {
        fprintf(stderr, "Error: Number of inodes and data blocks must be greater than zero.\n");
        print_usage(argv[0]);
    }

    //round data blocks to multiple of 32
    if (num_data_blocks % 32 != 0) {
        num_data_blocks = ((num_data_blocks / 32) + 1) * 32;
    }
}


//Since we are ignoring raid these two functions are the same:
//TODO: make sure we only use raid for datablocks themselves. even databitmap should ignore raid??
//keeping these functions separate for now in case I need to modify databitmap or pointers. 
void initialize_striped_disk(const char *disk_file, size_t disk_size, 
                             char *inode_bitmap, char *data_bitmap, 
                             struct wfs_inode root_inode, size_t disk_index) {
    int fd = open(disk_file, O_RDWR);
    if (fd == -1) {
        perror("Error opening disk file");
        exit(EXIT_FAILURE);
    }

    // Map disk into memory
    void *disk = mmap(NULL, disk_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (disk == MAP_FAILED) {
        perror("Error mapping disk file");
        close(fd);
        exit(EXIT_FAILURE);
    }

    // Write superblock
    struct wfs_sb striped_sb = sb;
    memcpy(disk, &striped_sb, sizeof(striped_sb));

    // Write bitmaps
    memcpy((char *)disk + striped_sb.i_bitmap_ptr, inode_bitmap, striped_sb.num_inodes / 8);
    memcpy((char *)disk + striped_sb.d_bitmap_ptr, data_bitmap, striped_sb.num_data_blocks / 8);

    // Write root inode
    //if (disk_index == 0) { // Root inode is only written to the first disk (or ignore raid for inodes as well? )
    memcpy((char *)disk + striped_sb.i_blocks_ptr, &root_inode, sizeof(root_inode));
   // }

    // Sync and unmap
    msync(disk, disk_size, MS_SYNC);
    munmap(disk, disk_size);
    close(fd);
}

void initialize_mirrored_disk(const char *disk_file, size_t disk_size, 
                              char *inode_bitmap, char *data_bitmap, 
                              struct wfs_inode root_inode) {
    int fd = open(disk_file, O_RDWR);
    if (fd == -1) {
        perror("Error opening disk file");
        exit(EXIT_FAILURE);
    }

    // Map disk into memory
    void *disk = mmap(NULL, disk_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (disk == MAP_FAILED) {
        perror("Error mapping disk file");
        close(fd);
        exit(EXIT_FAILURE);
    }

    // Write superblock
    memcpy(disk, &sb, sizeof(sb));

    // Write bitmaps
    memcpy((char *)disk + sb.i_bitmap_ptr, inode_bitmap, num_inodes / 8);
    memcpy((char *)disk + sb.d_bitmap_ptr, data_bitmap, num_data_blocks / 8);

    // Write root inode
    memcpy((char *)disk + sb.i_blocks_ptr, &root_inode, sizeof(root_inode));

    // Sync and unmap
    msync(disk, disk_size, MS_SYNC);
    munmap(disk, disk_size);
    close(fd);
}


void calculate_disk_layout(size_t *disk_size) {
    // Calculate total inodes and data blocks
    num_inodes = ((num_inodes + 31) / 32) * 32;
    num_data_blocks = ((num_data_blocks + 31) / 32) * 32;

    printf("Number of inodes: %zu\n", num_inodes);
    printf("Number of data blocks: %zu\n", num_data_blocks);

    //ignore raid in superblock
    // if (raid_mode == RAID0) {
    //     num_data_blocks /= num_disks;
    // }

    size_t inode_bitmap_size = num_inodes / 8;
    size_t data_bitmap_size = num_data_blocks / 8;
    printf("Inode bitmap size: %zu bytes\n", inode_bitmap_size);
    printf("Data bitmap size: %zu bytes\n", data_bitmap_size);

    sb.num_inodes = num_inodes;
    sb.num_data_blocks = num_data_blocks;
    sb.i_bitmap_ptr = BLOCK_SIZE; // Superblock is 512 bytes
    sb.d_bitmap_ptr = sb.i_bitmap_ptr + inode_bitmap_size;
    sb.i_blocks_ptr = (sb.d_bitmap_ptr + data_bitmap_size + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);
    sb.d_blocks_ptr = sb.i_blocks_ptr + (num_inodes * BLOCK_SIZE);
    sb.raid_mode = raid_mode;

    size_t total_size = sb.d_blocks_ptr + (num_data_blocks * BLOCK_SIZE);

    printf("Superblock:\n");
    printf("  Inode bitmap offset: %zu\n", sb.i_bitmap_ptr);
    printf("  Data bitmap offset: %zu\n", sb.d_bitmap_ptr);
    printf("  Inode region offset: %zu\n", sb.i_blocks_ptr);
    printf("  Data blocks offset: %zu\n", sb.d_blocks_ptr);

    for (size_t i = 0; i < num_disks; i++) {
        struct stat st;
        if (stat(disk_files[i], &st) == -1) {
            perror("Error accessing disk file");
            exit(EXIT_FAILURE);
        }
        printf("Disk %zu: %s, size: %zu bytes\n", i + 1, disk_files[i], st.st_size);
        if ((size_t)st.st_size < total_size) {
            fprintf(stderr, "Error: Disk file %s is too small. Minimum size: %zu bytes.\n", disk_files[i], total_size);
            exit(-1);
        }
        disk_size[i] = st.st_size;
    }
}

void initialize_disk(size_t *disk_size) {
    size_t inode_bitmap_size = num_inodes / 8; // Round up to nearest byte
    size_t data_bitmap_size = num_data_blocks / 8;
    char *inode_bitmap = calloc(1, inode_bitmap_size);
    char *data_bitmap = calloc(1, data_bitmap_size);

    if (!inode_bitmap || !data_bitmap) {
        perror("Error allocating memory for bitmaps");
        exit(EXIT_FAILURE);
    }

    // Initialize root inode
    inode_bitmap[0] |= 1; // Set the first bit of the first byte to 1

    struct wfs_inode root_inode = {
        .num = 0, //or 1??
        .mode = S_IFDIR | 0755, // File type and mode
        .uid = getuid(),
        .gid = getgid(),
        .size = 0,
        .nlinks = 2,
        .atim = time(NULL),
        .mtim = time(NULL),
        .ctim = time(NULL)
    };
    memset(root_inode.blocks, 0, sizeof(root_inode.blocks));

    //DEBUG
    printf("RAID MODE %d.\n", raid_mode);
    // RAID0: Initialize disks with striped data
    if (raid_mode == RAID0) {
        for (size_t i = 0; i < num_disks; i++) {
            initialize_striped_disk(disk_files[i], disk_size[i], inode_bitmap, data_bitmap, root_inode, i);
        }
    }

    // RAID1: Initialize disks with mirrored data
    else if (raid_mode == RAID1) {
        for (size_t i = 0; i < num_disks; i++) {
            initialize_mirrored_disk(disk_files[i], disk_size[i], inode_bitmap, data_bitmap, root_inode);
        }
    }

    // Free memory
    free(inode_bitmap);
    free(data_bitmap);

    printf("Disk initialization complete.\n");
}


int main(int argc, char *argv[]) {
    parse_args(argc, argv);

    //DEBUG 
    // printf("RAID mode: %d\n", raid_mode);
    // printf("Number of disks: %zu\n", num_disks);
    // for (size_t i = 0; i < num_disks; i++) {
    //     printf("Disk %zu: %s\n", i + 1, disk_files[i]);
    // }
    // printf("Number of inodes: %zu\n", num_inodes);
    // printf("Number of data blocks: %zu\n", num_data_blocks);

    // Disk sizes array
    size_t *disk_size = malloc(num_disks * sizeof(size_t));
    if (!disk_size) {
        perror("Error allocating memory for disk sizes");
        exit(EXIT_FAILURE);
    }

    calculate_disk_layout(disk_size);
    initialize_disk(disk_size);

    //free memory
    free(disk_files);
    free(disk_size);
    return 0;
}