#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>

#define BLOCK_SIZE 512 // Fixed block size
#define MAX_DISKS 10   // Max number of disks supported

// Superblock structure
struct superblock {
    char raid_mode;
    int inode_count;
    int block_count;
    int disk_count;
    int magic_number;
};

// Bitmap initialization
void initialize_bitmap(char *bitmap, int size) {
    memset(bitmap, 0, size);
}

int main(int argc, char *argv[]) {
    int opt;
    char raid_mode = -1;
    int inode_count = -1;
    int block_count = -1;
    char *disks[MAX_DISKS];
    int disk_count = 0;

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
                if (disk_count < MAX_DISKS) {
                    disks[disk_count++] = optarg;
                } else {
                    fprintf(stderr, "Too many disks specified (max %d).\n", MAX_DISKS);
                    exit(EXIT_FAILURE);
                }
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

    // Round block count to nearest multiple of 32
    if (block_count % 32 != 0) {
        block_count = ((block_count / 32) + 1) * 32;
    }

    // Initialize filesystem structures
    struct superblock sb = {
        .raid_mode = raid_mode,
        .inode_count = inode_count,
        .block_count = block_count,
        .disk_count = disk_count,
        .magic_number = 0x5376 // Example magic number
    };

    int bitmap_size = (inode_count + block_count) / 8 + 1; // Bytes needed for bitmaps

    // Open and initialize disks
    for (int i = 0; i < disk_count; i++) {
        int fd = open(disks[i], O_RDWR);
        if (fd < 0) {
            fprintf(stderr, "Error: Cannot open disk file %s.\n", disks[i]);
            exit(EXIT_FAILURE);
        }

        // Map disk image into memory
        void *disk_map = mmap(NULL, block_count * BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (disk_map == MAP_FAILED) {
            fprintf(stderr, "Error mapping disk %s: %s\n", disks[i], strerror(errno));
            close(fd);
            exit(EXIT_FAILURE);
        }

        // Write superblock
        memcpy(disk_map, &sb, sizeof(sb));

        // Initialize inode and block bitmaps
        char *inode_bitmap = (char *)disk_map + sizeof(sb);
        char *block_bitmap = inode_bitmap + bitmap_size / 2; // Assuming equal size for inodes and blocks
        initialize_bitmap(inode_bitmap, bitmap_size / 2);
        initialize_bitmap(block_bitmap, bitmap_size / 2);

        // Initialize root inode (simple example, adjust as needed)
        struct {
            int size;
            int direct_blocks[10];
            int indirect_block;
        } root_inode = {
            .size = 0,
            .direct_blocks = {0},
            .indirect_block = 0
        };
        memcpy((char *)disk_map + sizeof(sb) + bitmap_size, &root_inode, sizeof(root_inode));

        printf("Initialized disk %s with superblock and root inode.\n", disks[i]);

        munmap(disk_map, block_count * BLOCK_SIZE);
        close(fd);
    }

    return 0;
}