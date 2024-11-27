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

    // Free allocated memory
    free(disk_files);

    return 0;
}