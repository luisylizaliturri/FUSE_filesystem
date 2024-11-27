#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
    int opt;
    char raid_mode = -1; // RAID mode (-r 0 or -r 1)
    int inode_count = -1; // Number of inodes (-i)
    int block_count = -1; // Number of data blocks (-b)
    char **disks = malloc(argc * sizeof(char *)); // Disk image filenames
    int disk_count = 0;

    // Parse arguments
    while ((opt = getopt(argc, argv, "r:d:i:b:")) != -1) {
        switch (opt) {
            case 'r':
                if (strcmp(optarg, "0") == 0 || strcmp(optarg, "1") == 0 || strcmp(optarg, "1v") == 0) {
                    raid_mode = optarg[0];
                } else {
                    fprintf(stderr, "Invalid RAID mode. Use 0, 1, or 1v.\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'd':
                disks[disk_count++] = optarg;
                break;
            case 'i':
                inode_count = atoi(optarg);
                if (inode_count <= 0) {
                    fprintf(stderr, "Invalid number of inodes.\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'b':
                block_count = atoi(optarg);
                if (block_count <= 0) {
                    fprintf(stderr, "Invalid number of data blocks.\n");
                    exit(EXIT_FAILURE);
                }
                break;
            default:
                fprintf(stderr, "Usage: %s -r <raid_mode> -d <disk_image> -i <num_inodes> -b <num_blocks>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Validate arguments
    if (raid_mode == -1 || disk_count == 0 || inode_count == -1 || block_count == -1) {
        fprintf(stderr, "Missing required arguments.\n");
        exit(EXIT_FAILURE);
    }
    // Round block count to the nearest multiple of 32
    if (block_count % 32 != 0) {
        block_count = ((block_count / 32) + 1) * 32;
    }
    printf("Adjusted block count: %d\n", block_count);

    // Validate each disk
    struct stat disk_stat;
    for (int i = 0; i < disk_count; i++) {
        int fd = open(disks[i], O_RDWR);
        if (fd < 0) {
            fprintf(stderr, "Error: Cannot open disk file %s.\n", disks[i]);
            free(disks);
            exit(EXIT_FAILURE);
        }

        // Check disk size
        if (fstat(fd, &disk_stat) == -1) {
            fprintf(stderr, "Error: Cannot get size of disk file %s.\n", disks[i]);
            close(fd);
            free(disks);
            exit(EXIT_FAILURE);
        }
        if (disk_stat.st_size < block_count * 512) {
            fprintf(stderr, "Error: Disk file %s is too small to hold the filesystem.\n", disks[i]);
            close(fd);
            free(disks);
            exit(EXIT_FAILURE);
        }

        printf("Disk %d (%s) is valid, size: %ld bytes.\n", i + 1, disks[i], disk_stat.st_size);
        close(fd);
    }

    // Print parsed arguments for debugging
    printf("RAID mode: %c\n", raid_mode);
    printf("Number of disks: %d\n", disk_count);
    printf("Inodes: %d\n", inode_count);
    printf("Data blocks: %d\n", block_count);
    for (int i = 0; i < disk_count; i++) {
        printf("Disk %d: %s\n", i + 1, disks[i]);
    }

    // Clean up
    free(disks);
    return 0;
}
