#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "wfs.h"

// Constants
#define MIN_DISKS 2
#define RAID0 0
#define RAID1 1
#define RAID1V 2

// Global variables for RAID and other inputs
int raid_mode = -1;
char **disk_files = NULL;
size_t num_disks = 0;
size_t num_inodes = 0;
size_t num_data_blocks = 0;

// Function to print usage
void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s -r <raid_mode> -d <disk_file> [-d <disk_file> ...] -i <num_inodes> -b <num_data_blocks>\n", prog_name);
    fprintf(stderr, "  <raid_mode>: 0 for RAID 0, 1 for RAID 1, 1v for RAID 1v\n");
    exit(EXIT_FAILURE);
}

// Function to parse command-line arguments
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
                fprintf(stderr, "Error: Invalid RAID mode. Use 0, 1, or 1v.\n");
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

    // Post-processing validation
    if (raid_mode == -1) {
        fprintf(stderr, "Error: RAID mode not specified.\n");
        print_usage(argv[0]);
    }
    if (num_disks < MIN_DISKS) {
        fprintf(stderr, "Error: At least %d disk files are required.\n", MIN_DISKS);
        print_usage(argv[0]);
    }
    if (num_inodes == 0 || num_data_blocks == 0) {
        fprintf(stderr, "Error: Number of inodes and data blocks must be greater than zero.\n");
        print_usage(argv[0]);
    }

    // Round data blocks to nearest multiple of 32
    if (num_data_blocks % 32 != 0) {
        num_data_blocks = ((num_data_blocks / 32) + 1) * 32;
    }
}

void calculate_disk_layout(size_t *disk_size) {
    size_t total_size = 0;
    size_t inode_bitmap_size = (num_inodes + 7) / 8; // Round up to nearest byte
    size_t data_bitmap_size = (num_data_blocks + 7) / 8; // Round up to nearest byte
    inode_bitmap_size = (inode_bitmap_size + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1); // Align to 512 bytes
    data_bitmap_size = (data_bitmap_size + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1); // Align to 512 bytes

    size_t inode_region_size = num_inodes * BLOCK_SIZE; // Each inode is 512 bytes
    size_t data_region_size = num_data_blocks * BLOCK_SIZE; // Data blocks

    total_size = BLOCK_SIZE + // Superblock
                 inode_bitmap_size + // Inode bitmap
                 data_bitmap_size + // Data bitmap
                 inode_region_size + // Inode region
                 data_region_size;   // Data blocks

    // Ensure all disks are large enough
    for (size_t i = 0; i < num_disks; i++) {
        struct stat st;
        if (stat(disk_files[i], &st) == -1) {
            perror("Error accessing disk file");
            exit(EXIT_FAILURE);
        }
        if ((size_t)st.st_size < total_size) {
            fprintf(stderr, "Error: Disk file %s is too small. Minimum size: %zu bytes.\n", disk_files[i], total_size);
            exit(EXIT_FAILURE);
        }
        disk_size[i] = st.st_size;
    }

    // Update superblock pointers
    struct wfs_sb sb;
    sb.num_inodes = num_inodes;
    sb.num_data_blocks = num_data_blocks;
    sb.i_bitmap_ptr = BLOCK_SIZE;
    sb.d_bitmap_ptr = sb.i_bitmap_ptr + inode_bitmap_size;
    sb.i_blocks_ptr = sb.d_bitmap_ptr + data_bitmap_size;
    sb.d_blocks_ptr = sb.i_blocks_ptr + inode_region_size;

    printf("Superblock:\n");
    printf("  Inode bitmap offset: %zu\n", sb.i_bitmap_ptr);
    printf("  Data bitmap offset: %zu\n", sb.d_bitmap_ptr);
    printf("  Inode region offset: %zu\n", sb.i_blocks_ptr);
    printf("  Data blocks offset: %zu\n", sb.d_blocks_ptr);
    printf("  Total size: %zu bytes\n", total_size);
}

int main(int argc, char *argv[]) {
    parse_args(argc, argv);

    // For now, print parsed arguments for debugging
    printf("RAID mode: %d\n", raid_mode);
    printf("Number of disks: %zu\n", num_disks);
    for (size_t i = 0; i < num_disks; i++) {
        printf("Disk %zu: %s\n", i + 1, disk_files[i]);
    }
    printf("Number of inodes: %zu\n", num_inodes);
    printf("Number of data blocks: %zu\n", num_data_blocks);

    // Disk sizes array
    size_t *disk_size = malloc(num_disks * sizeof(size_t));
    if (!disk_size) {
        perror("Error allocating memory for disk sizes");
        exit(EXIT_FAILURE);
    }

    calculate_disk_layout(disk_size);

    // Free allocated memory
    free(disk_files);
    free(disk_size);
    return 0;
}