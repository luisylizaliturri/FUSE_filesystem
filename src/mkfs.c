#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "wfs.h"
#include <sys/mman.h>
#include <getopt.h>
#include <errno.h>

#define MIN_DISKS 2
#define RAID0 0
#define RAID1 1
#define RAID1V 2

//Block size is always 512 bytes (according to instructions)
int main(int argc, char **argv) {
    struct wfs_sb super_block;
    int num_blocks = -1;
    int num_inodes = -1;
    int raid_mode = -1;
    int num_disks = 0;
    char **disk_files = NULL;
    int opt;

    //parse and validate arguments

    while ((opt = getopt(argc, argv, "d:i:b:r:")) != -1) {
        switch (opt) {
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
                if (num_inodes <= 0) {
                    fprintf(stderr, "Invalid number of inodes\n");
                    exit(EXIT_FAILURE);
                }
                break;
            
            case 'b':
                num_blocks = atoi(optarg);
                if (num_blocks <= 0) {
                    fprintf(stderr, "Invalid number of blocks\n");
                    exit(EXIT_FAILURE);
                }
                break;
            
            case 'r':
                if (strcmp(optarg, "0") == 0) {
                    raid_mode = RAID0;
                } else if (strcmp(optarg, "1") == 0) {
                    raid_mode = RAID1;
                } else if (strcmp(optarg, "1v") == 0) {
                    raid_mode = RAID1V;
                } else {
                    fprintf(stderr, "Invalid RAID mode. Must be 0, 1, or 1v\n");
                    exit(EXIT_FAILURE);
                }
                break;
            
            default:
                fprintf(stderr, "Usage: %s -d disk_file [-d disk_file ...] -i num_inodes -b num_blocks -r raid_mode\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (num_disks < MIN_DISKS) {
        fprintf(stderr, "Error: At least %d disk files are required.\n", MIN_DISKS);
        exit(EXIT_FAILURE);
    }

    if (num_inodes <= 0 || num_blocks <= 0) {
        fprintf(stderr, "Error: Number of inodes and data blocks must be greater than zero.\n");
        exit(EXIT_FAILURE);
    }

    // round up num blocks to nearest higher multiple of 32
    if (num_blocks % 32 != 0)
        num_blocks = (num_blocks - num_blocks % 32) + 32;
    if (num_inodes % 32 != 0)
        num_inodes = (num_inodes - num_inodes % 32) + 32;



    //Check disk sizes
    size_t required_size = 
    BLOCK_SIZE +                    //superblock
    (num_inodes / 8) +             //inode bitmap
    (num_blocks / 8) +             //data block bitmap
    (num_inodes * BLOCK_SIZE) +    //inode blocks region
    (num_blocks * BLOCK_SIZE);     //data blocks region

    //round up to block alignment
    required_size = (required_size + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);

    size_t *disk_sizes = malloc(num_disks * sizeof(size_t));
    if (!disk_sizes) {
        perror("Error allocating memory for disk sizes");
        exit(EXIT_FAILURE);
    }

    for (size_t i = 0; i < num_disks; i++) {
        struct stat st; //store file stats
        if (stat(disk_files[i], &st) == -1) {
            perror("Error accessing disk file");
            exit(EXIT_FAILURE);
        }
        printf("Disk %zu: %s, size: %zu bytes\n", i + 1, disk_files[i], st.st_size);
        if ((size_t)st.st_size < required_size) {
            fprintf(stderr, "Error: Disk file %s is too small. Minimum size: %zu bytes.\n", disk_files[i], required_size);
            exit(-1);
        }
        disk_sizes[i] = st.st_size;
    }

    //initialize super block
    super_block.num_data_blocks = num_blocks;
    super_block.num_inodes = num_inodes;
    super_block.raid_mode = raid_mode;
    super_block.i_bitmap_ptr = BLOCK_SIZE;
    super_block.d_bitmap_ptr = super_block.i_bitmap_ptr + (num_inodes / 8);
    //these should be block aligned
    super_block.i_blocks_ptr = (super_block.d_bitmap_ptr + (num_blocks / 8) + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);
    super_block.d_blocks_ptr = super_block.i_blocks_ptr + (num_inodes * BLOCK_SIZE);

    //initialize root inode
    struct wfs_inode root_inode;
    root_inode.num = 0;
    root_inode.mode = S_IFDIR | 0755;
    root_inode.uid = getuid();
    root_inode.gid = getgid();
    root_inode.size = 0; //lazy allocation for all inodes
    root_inode.nlinks = 2; //for . and ..
    root_inode.atim = root_inode.mtim = root_inode.ctim = time(NULL);
    memset(root_inode.blocks, 0, N_BLOCKS * sizeof(off_t));


    //initialize disks and add super block to each disk and root inode to each disk
    //Only doing raid for datablocks not inodes or any other metadata as per the instructions
    for (size_t i = 0; i < num_disks; i++) {
        struct wfs_sb disk_sb = super_block;
        disk_sb.disk_id = i;  // Assign disk ID in order disks were specified
        
        int fd = open(disk_files[i], O_RDWR);
        if (fd < 0) {
            perror("Error opening disk file");
            exit(EXIT_FAILURE);
        }

        struct stat st;
        if (fstat(fd, &st) == -1) {
            perror("Error getting disk file size");
            exit(EXIT_FAILURE);
        }

        char *disk = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (disk == MAP_FAILED) {
            perror("Error mapping disk file");
            exit(EXIT_FAILURE);
        }

        //copy super block to disk
        memcpy(disk, &disk_sb, BLOCK_SIZE);

        //set inode bitmap to 1 for root inode
        char *inode_bitmap = disk + super_block.i_bitmap_ptr;
        inode_bitmap[0] |= 1;

        //copy root inode to disk
        //Should this table by BLOCK_SIZE aligned?
        char *inode_table = disk + super_block.i_blocks_ptr;
        memset(inode_table, 0, BLOCK_SIZE);
        memcpy(inode_table, &root_inode, sizeof(struct wfs_inode));

        // Zero out entire data block region
        char *data_region = disk + super_block.d_blocks_ptr;
        size_t data_region_size = super_block.num_data_blocks * BLOCK_SIZE;
        memset(data_region, 0, data_region_size);
        close(fd);
    }

    //DEBUG print super block
    // printf("Superblock:\n");
    // printf("  Inode bitmap offset: %zu\n", super_block.i_bitmap_ptr);
    // printf("  Data bitmap offset: %zu\n", super_block.d_bitmap_ptr);
    // printf("  Inode region offset: %zu\n", super_block.i_blocks_ptr);
    // printf("  Data blocks offset: %zu\n", super_block.d_blocks_ptr);

    free(disk_sizes);
    free(disk_files);
    return 0;
}