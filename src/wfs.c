#define FUSE_USE_VERSION 30

#include "wfs.h"
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <limits.h>

#define MIN_DISKS 2
#define RAID0 0
#define RAID1 1
#define RAID1V 2

#define ENTRIES_PER_BLOCK (BLOCK_SIZE / sizeof(struct wfs_dentry))
#define POINTERS_PER_BLOCK (BLOCK_SIZE / sizeof(off_t))


//==================HELPER FUNCTION PROTOTYPES=======================//

static inline off_t get_raid0_disk_offset(off_t offset);
static size_t get_raid0_disk_index(off_t block_num);
static off_t get_raid0_block_offset(off_t block_num);
struct wfs_dentry *find_dir_entry(struct wfs_inode *dir_inode, const char *name);
struct wfs_inode *get_inode(const char *path);
char *get_parent_path(const char *path);
char *get_file_name(const char *path);
int allocate_data_block();
struct wfs_inode *allocate_inode(mode_t mode);
int add_entry_to_parent_directory(struct wfs_inode *parent, const char *name, int inode_num);
int handle_inode_insertion(const char *path, mode_t mode);
static int remove_dir_entry(struct wfs_inode *parent, const char *name);
static void free_data_blocks(struct wfs_inode *inode);
static void free_inode(struct wfs_inode *inode);
void debug_print_inode_bitmap();
void debug_print_inodes(int disk_idx);
void debug_print_data_bitmap();
void debug_dump_data_regions();



//==================GLOBAL VARIABLES=======================//




struct wfs_sb super_block;  //first super block
size_t num_disks = 0;
int raid_mode = -1;
char **disk_files = NULL; // Array of disk file names
void **disk_map = NULL; // Array of disk pointers

static size_t next_raid0_disk = 0; // Next disk to allocate datablock to in RAID0 mode




//======================HELPER FUNCTIONS===========================//



static inline off_t get_raid0_disk_offset(off_t offset) {
    size_t stripe_number = offset / (BLOCK_SIZE * num_disks);
    return (stripe_number * BLOCK_SIZE) + super_block.d_blocks_ptr;
}

static size_t get_raid0_disk_index(off_t block_num) {
    return block_num % num_disks;
}

static off_t get_raid0_block_offset(off_t block_num) {
    return (block_num / num_disks) * BLOCK_SIZE + super_block.d_blocks_ptr;
}

//returns pointer to dir entry 
struct wfs_dentry *find_dir_entry(struct wfs_inode *dir_inode, const char *name) {
    printf("find_dir_entry(): looking for %s\n", name);
    if (!dir_inode) {
        return NULL;
    }
    int entries_per_block = BLOCK_SIZE / sizeof(struct wfs_dentry);
    // Check all direct blocks (except last indirect block)
    for (int block_idx = 0; block_idx < N_BLOCKS - 1; block_idx++) {
        if (dir_inode->blocks[block_idx] == 0) {
            continue;  // Skip unallocated blocks
        }
        size_t disk_idx;
        off_t block_offset;
        if (raid_mode == RAID0) {
            disk_idx = get_raid0_disk_index(dir_inode->blocks[block_idx] - 1);
            block_offset = get_raid0_block_offset(dir_inode->blocks[block_idx] - 1);
        } else {
            disk_idx = 0;
            block_offset = super_block.d_blocks_ptr + 
                          ((dir_inode->blocks[block_idx] - 1) * BLOCK_SIZE);
        }
        struct wfs_dentry *entries = (struct wfs_dentry *)((char *)disk_map[disk_idx] + block_offset);
        // Calculate entries in this block
        int start_entry = block_idx * entries_per_block;
        int remaining_entries = (dir_inode->size / sizeof(struct wfs_dentry)) - start_entry;
        int num_entries = remaining_entries < entries_per_block ? remaining_entries : entries_per_block;
        for (int i = 0; i < num_entries; i++) {
            if (strcmp(entries[i].name, name) == 0) {
                // printf("find_dir_entry(): found %s at block %d, index %d\n", 
                //        name, block_idx, i);
                return &entries[i];
            }
        }
    }
    // printf("find_dir_entry(): %s not found in any block\n", name);
    return NULL;
}

struct wfs_inode *get_inode(const char *path) {
    //printf("get_inode called: path: %s\n", path);
    
    if (strcmp(path, "/") == 0) {
        struct wfs_inode *inode_table = (struct wfs_inode *)((char *)disk_map[0] + super_block.i_blocks_ptr);
        return &inode_table[0];
    }

    struct wfs_inode *current_inode = get_inode("/"); // Start at root inode
    char *path_copy = strdup(path);
    char *saveptr;
    char *component = strtok_r(path_copy, "/", &saveptr); //
    char *next_component = NULL;
    
    while (component != NULL) {
        if (strlen(component) == 0) {
            component = strtok_r(NULL, "/", &saveptr);
            continue;
        }

        // Peek at next component to check if we're at last one
        next_component = strtok_r(NULL, "/", &saveptr);
        
        // Only check for directory if this isn't the last component
        if (next_component != NULL && !S_ISDIR(current_inode->mode)) {
            free(path_copy);
            return NULL;
        }

        struct wfs_dentry *entry = find_dir_entry(current_inode, component);

        if (!entry) {
            printf("get_inode() NOT FOUND: path: %s, component: %s\n", path, component);
            free(path_copy);
            return NULL;
        }
        //debug print
        //printf("get_inode(): path: %s, entry_num: %d\n", path, entry->num);
        current_inode = (struct wfs_inode *)((char *)disk_map[0] + super_block.i_blocks_ptr + (entry->num * BLOCK_SIZE));
        component = next_component;
        //print current inode
        printf("get_inode(): path: %s, inode_num: %d\n", path, current_inode->num);
    }
    printf("get_inode(): path: %s, inode_num: %d\n", path, current_inode->num);
    free(path_copy);
    return current_inode;
}

char *get_parent_path(const char *path){
    int last_slash_index;
    for (int i = 0; i < strlen(path) - 1; i++){
        if (path[i] == '/'){
            last_slash_index = i;
        }
    }
    char *parent_path = strdup(path);
    // seperate the parent path and child path
    parent_path[last_slash_index + 1] = '\0';
    return parent_path;
}

char *get_file_name(const char *path){
    int last_slash_index;
    for (int i = 0; i < strlen(path) - 1; i++){
        if (path[i] == '/'){
            last_slash_index = i;
        }
    }
    char *file_name = strdup(path + last_slash_index + 1);
    return file_name;
}

//Allocate a new data block by updating bitmap disks based on raid mode
int allocate_data_block() {
    if (raid_mode == RAID0) {
        // Try each disk starting from next_raid0_disk
        for (size_t attempts = 0; attempts < num_disks; attempts++) {
            size_t current_disk = next_raid0_disk;
            char *disk_bitmap = (char *)disk_map[current_disk] + super_block.d_bitmap_ptr;
            
            // Find free block in this disk's bitmap
            for (int i = 0; i < (super_block.num_data_blocks / 8); i++) {
                for (int j = 0; j < 8; j++) {
                    if (((disk_bitmap[i] >> j) & 1) == 0) {
                        // Found free block on current disk
                        int local_block = (i * 8) + j;
                        disk_bitmap[i] |= (1 << j);
                        
                        // Update next disk (round-robin)
                        next_raid0_disk = (current_disk + 1) % num_disks;
                        
                        // Return global block number
                        return (local_block * num_disks) + current_disk;
                    }
                }
            }
            // Try next disk
            next_raid0_disk = (next_raid0_disk + 1) % num_disks;
        }
    } else {
        // RAID1: Original implementation
        char *first_bitmap = (char *)disk_map[0] + super_block.d_bitmap_ptr;
        for (int i = 0; i < (super_block.num_data_blocks / 8); i++) {
            for (int j = 0; j < 8; j++) {
                if (((first_bitmap[i] >> j) & 1) == 0) {
                    int block_num = (i * 8) + j;
                    // Mark block allocated on all disks
                    for (size_t disk = 0; disk < num_disks; disk++) {
                        char *bitmap = (char *)disk_map[disk] + super_block.d_bitmap_ptr;
                        bitmap[i] |= (1 << j);
                    }
                    return block_num;
                }
            }
        }
    }
    return -ENOSPC;
}

//Allocate a new inode on each disk
//This function only updates bitmap and inode table on each disk (does not update parent directory)
struct wfs_inode *allocate_inode(mode_t mode) {
    int idx = -1;
    struct wfs_inode *inode_ptr = NULL;
    
    //Find a free inode number
    char *first_bitmap = (char *)disk_map[0] + super_block.i_bitmap_ptr;
    for (int i = 0; i < (super_block.num_inodes / 8); i++) {
        char *currByte = (first_bitmap + i);
        for (int j = 0; j < 8; j++) {
            if (((*currByte >> j) & 1) == 0) {
                idx = (i * 8) + j;
                goto found_idx;
            }
        }
    }
    return NULL;  // No free inodes

found_idx:
    // Update all disks with new inode
    for (size_t disk = 0; disk < num_disks; disk++) {
        // Set bitmap
        char *bitmap = (char *)disk_map[disk] + super_block.i_bitmap_ptr;
        bitmap[idx / 8] |= (1 << (idx % 8));

        // Get pointer to full inode block
        char *inode_block = (char *)disk_map[disk] + super_block.i_blocks_ptr + (idx * BLOCK_SIZE);
        //Zero entire block first
        memset(inode_block, 0, BLOCK_SIZE);
            
        // Initialize inode at start of block
        struct wfs_inode *disk_inode = (struct wfs_inode *)inode_block;
        disk_inode->num = idx;
        disk_inode->mode = mode;
        disk_inode->uid = getuid();
        disk_inode->gid = getgid();
        disk_inode->size = 0;
        disk_inode->nlinks = S_ISDIR(mode) ? 2 : 1; //nlink = 2 if mode is directory
        disk_inode->atim = time(NULL);
        disk_inode->mtim = time(NULL);
        disk_inode->ctim = time(NULL);
        memset(disk_inode->blocks, 0, N_BLOCKS * sizeof(off_t));
                
        if (disk == 0) {
            inode_ptr = disk_inode;  // Save pointer from first disk
        }
    }
    //printf("Allocated new inode: index: %d\n", idx);
    return inode_ptr; // Return pointer to inode on first disk only
}

//Add new entry to parent directory block based on raid mode
int add_entry_to_parent_directory(struct wfs_inode *parent, const char *name, int inode_num) {
    // printf("Adding entry: %s (inode %d) to parent directory\n", name, inode_num);
    // printf("Parent initial size: %ld\n", parent->size);
    // printf("Parent blocks[0]: %ld\n", parent->blocks[0]);

    //traverse all allocated blocks in the parent inode
    for (int block_idx = 0; block_idx < N_BLOCKS - 1; block_idx++) {
        if (parent->blocks[block_idx] == 0) { //no allocated blocks
            int block_num = allocate_data_block(); //allocate new data block based on raid mode
            if (block_num < 0) {
                return -ENOSPC;
            }
            parent->blocks[block_idx] = block_num + 1;
        }

        if (raid_mode == RAID0) { // RAID0
            size_t disk_idx = get_raid0_disk_index(parent->blocks[block_idx] -1);
            off_t block_offset = get_raid0_block_offset(parent->blocks[block_idx] -1);

            //debug print
            // printf("Parent block offset: %ld\n", block_offset);
            // printf("Parent disk index: %zu\n", disk_idx);
            
            struct wfs_dentry *entries = (struct wfs_dentry *)((char *)disk_map[disk_idx] + block_offset);
            for (int i = 0; i < ENTRIES_PER_BLOCK; i++) {
                if (entries[i].num == 0) {
                    strncpy(entries[i].name, name, MAX_NAME);
                    entries[i].num = inode_num;
                    parent->size += sizeof(struct wfs_dentry);
                    parent->nlinks++;
                    return 0;
                }
            }
        } else { // RAID1/RAID1V
            off_t block_offset = super_block.d_blocks_ptr + 
                        ((parent->blocks[block_idx] - 1) * BLOCK_SIZE);

            //get first disk entries
            struct wfs_dentry *first_entries = (struct wfs_dentry *)((char *)disk_map[0] + block_offset);
            
            //traverse all entries in the block
            for (int i = 0; i < ENTRIES_PER_BLOCK; i++) {
                if (first_entries[i].num == 0) { //free entry spot
                    //update all disks
                    for (size_t disk = 0; disk < num_disks; disk++) {
                        struct wfs_dentry *entries = (struct wfs_dentry *)((char *)disk_map[disk] + block_offset);
                        strncpy(entries[i].name, name, MAX_NAME);
                        entries[i].num = inode_num;
                    }
                    parent->size += sizeof(struct wfs_dentry);
                    parent->nlinks++;
                    return 0;
                }
            }
        }
    }
    return -ENOSPC;
}

// Helper to get/create indirect block
static off_t *get_indirect_block(struct wfs_inode *inode) {
    for(int i = 0; i < N_BLOCKS; i++){
        printf("inode->blocks[%d]: %ld\n", i, inode->blocks[i]);
    }


    if (inode->blocks[N_BLOCKS-1] == 0) {
        int new_block_num = allocate_data_block(); // Allocate new data block based on raid mode
        if (new_block_num < 0){
            printf("Error allocating new data block\n");
            return NULL;
        }
        inode->blocks[N_BLOCKS-1] = new_block_num + 1;
        
        //Initialize indirect block
        if(raid_mode == RAID0) {
            size_t disk_idx = get_raid0_disk_index(new_block_num);
            off_t block_addr = get_raid0_block_offset(new_block_num);
            memset((char *)disk_map[disk_idx] + block_addr, 0, BLOCK_SIZE);
        }else{
            off_t block_addr = super_block.d_blocks_ptr + (new_block_num * BLOCK_SIZE);
            for (size_t disk = 0; disk < num_disks; disk++) {
                memset((char *)disk_map[disk] + block_addr, 0, BLOCK_SIZE);
            }
        }
    }

    // Return pointer to indirect block
    // For RAID1, maintain a local copy of indirect block entries
    // and update all disks when modified
    static off_t local_indirect[BLOCK_SIZE/sizeof(off_t)];
    
    // Get pointer to indirect block
    if (raid_mode == RAID0) {
        size_t disk_idx = get_raid0_disk_index(inode->blocks[N_BLOCKS-1] - 1);
        off_t block_addr = get_raid0_block_offset(inode->blocks[N_BLOCKS-1] - 1);
        return (off_t *)((char *)disk_map[disk_idx] + block_addr);
    } else {
        // // Read from first disk into local buffer
        off_t block_addr = super_block.d_blocks_ptr + ((inode->blocks[N_BLOCKS-1] - 1) * BLOCK_SIZE);
        memcpy(local_indirect, (char *)disk_map[0] + block_addr, BLOCK_SIZE);
        
        // Mirror changes to all disks when indirect block is modified
        for (size_t disk = 1; disk < num_disks; disk++) {
            memcpy((char *)disk_map[disk] + block_addr, local_indirect, BLOCK_SIZE);
        }
        return local_indirect;
    }
}


int handle_inode_insertion(const char *path, mode_t mode) {
    char *file_name = get_file_name(path);
    char *parent_path = get_parent_path(path);
    
    // Get parent inode
    struct wfs_inode *parent = get_inode(parent_path);
    if (parent == NULL) {
        return -ENOENT;
    }

    // Allocate new inode
    struct wfs_inode *new_inode = allocate_inode(mode);
    if (new_inode == NULL) {
        return -ENOSPC;
    }


    int is_inserted = add_entry_to_parent_directory(parent, file_name, new_inode->num);
    if (is_inserted < 0) {
        printf("Error adding entry to parent directory\n");
        return is_inserted;
    }


    //Update parent inode on all disks
    for (size_t disk = 0; disk < num_disks; disk++) {
        // Calculate block-aligned offset for parent inode
        char *inode_block = (char *)disk_map[disk] + super_block.i_blocks_ptr + 
                        (parent->num * BLOCK_SIZE);
        // Write parent inode at start of its block
        memcpy(inode_block, parent, sizeof(struct wfs_inode));
    }
    free(file_name);
    free(parent_path);
    return 0;
}


//Helper to remove directory entry from parent
static int remove_dir_entry(struct wfs_inode *parent, const char *name) {
    // Find entry using existing helper
    struct wfs_dentry *entry = find_dir_entry(parent, name);
    if (!entry) {
        return -ENOENT;
    }
    // Calculate block offset and entry position
    off_t block_offset = super_block.d_blocks_ptr + 
                        ((parent->blocks[0] - 1) * BLOCK_SIZE);
    size_t entry_offset = (char *)entry - ((char *)disk_map[0] + block_offset);

    if (raid_mode == RAID0) {
        // Just clear entry in single disk for RAID0
        memset(entry, 0, sizeof(struct wfs_dentry));
    } else {
        // Clear entry in all disks for RAID1
        for (size_t disk = 0; disk < num_disks; disk++) {
            struct wfs_dentry *disk_entry = (struct wfs_dentry *)
                ((char *)disk_map[disk] + block_offset + entry_offset);
            memset(disk_entry, 0, sizeof(struct wfs_dentry));
        }
    }

    // Update parent metadata
    parent->size -= sizeof(struct wfs_dentry);
    parent->nlinks--;

    // Update parent inode on all disks
    for (size_t disk = 0; disk < num_disks; disk++) {
        char *inode_block = (char *)disk_map[disk] + super_block.i_blocks_ptr + 
                           (parent->num * BLOCK_SIZE);
        memcpy(inode_block, parent, sizeof(struct wfs_inode));
    }

    return 0;
}

//Helper to free data blocks
static void free_data_blocks(struct wfs_inode *inode) {
    // Handle direct blocks
    for (int i = 0; i < N_BLOCKS - 1; i++) {
        if (inode->blocks[i] == 0) continue; // Skip unallocated blocks

        if (raid_mode == RAID0) {
            size_t disk_idx = get_raid0_disk_index(inode->blocks[i] - 1);
            char *disk_bitmap = (char *)disk_map[disk_idx] + super_block.d_bitmap_ptr;
            int local_block = (inode->blocks[i] - 1) / num_disks;
            disk_bitmap[local_block / 8] &= ~(1 << (local_block % 8));
        } else {
            int block_num = inode->blocks[i] - 1;
            for (size_t disk = 0; disk < num_disks; disk++) {
                char *disk_bitmap = (char *)disk_map[disk] + super_block.d_bitmap_ptr;
                disk_bitmap[block_num / 8] &= ~(1 << (block_num % 8));
            }
        }
    }

    //DEBUG
    // for(int i = 0; i < N_BLOCKS; i++){
    //     printf("inode->blocks[%d]: %ld\n", i, inode->blocks[i]);
    // }
    // printf("Freeing direct blocks\n");
    // debug_print_data_bitmap();

    // Handle indirect block
    if (inode->blocks[N_BLOCKS-1] != 0) {
        off_t *indirect_ptrs = get_indirect_block(inode);
        for (size_t i = 0; i < POINTERS_PER_BLOCK; i++) {
            printf("%ld ", indirect_ptrs[i]);
        }
        // Clear indirect block's data blocks
        for (size_t i = 0; i < POINTERS_PER_BLOCK; i++) {
            if (indirect_ptrs[i] == 0) continue; // Skip unallocated blocks
            if (raid_mode == RAID0) {
                size_t disk_idx = get_raid0_disk_index(indirect_ptrs[i] - 1);
                char *disk_bitmap = (char *)disk_map[disk_idx] + super_block.d_bitmap_ptr;
                int local_block = (indirect_ptrs[i] - 1) / num_disks;
                disk_bitmap[local_block / 8] &= ~(1 << (local_block % 8));
            } else {
                int block_num = indirect_ptrs[i] - 1;
                for (size_t disk = 0; disk < num_disks; disk++) {
                    char *disk_bitmap = (char *)disk_map[disk] + super_block.d_bitmap_ptr;
                    disk_bitmap[block_num / 8] &= ~(1 << (block_num % 8));
                }
            }
        }

        // Clear indirect block itself
        if (raid_mode == RAID0) {
            size_t disk_idx = get_raid0_disk_index(inode->blocks[N_BLOCKS-1] - 1);
            char *disk_bitmap = (char *)disk_map[disk_idx] + super_block.d_bitmap_ptr;
            int local_block = (inode->blocks[N_BLOCKS-1] - 1) / num_disks;
            disk_bitmap[local_block / 8] &= ~(1 << (local_block % 8));
        } else {
            int block_num = inode->blocks[N_BLOCKS-1] - 1;
            for (size_t disk = 0; disk < num_disks; disk++) {
                char *disk_bitmap = (char *)disk_map[disk] + super_block.d_bitmap_ptr;
                disk_bitmap[block_num / 8] &= ~(1 << (block_num % 8));
            }
        }
    }
}

//Helper to free inode
static void free_inode(struct wfs_inode *inode) {
    // Clear inode bitmap on all disks
    for (size_t disk = 0; disk < num_disks; disk++) {
        char *inode_bitmap = (char *)disk_map[disk] + super_block.i_bitmap_ptr;
        inode_bitmap[inode->num / 8] &= ~(1 << (inode->num % 8));
    }
    free_data_blocks(inode);
}



//======================DEBUG FUNCTIONS===========================//



void debug_print_inode_bitmap() {
    printf("\n=== Inode Bitmap Contents ===\n");
    
    for (size_t disk = 0; disk < num_disks; disk++) {
        printf("\nDisk %zu:\n", disk);
        char *inode_bitmap = (char *)disk_map[disk] + super_block.i_bitmap_ptr;
        
        for (int i = 0; i < super_block.num_inodes; i++) {
            int byte_offset = i / 8;
            int bit_position = i % 8;
            int is_allocated = (inode_bitmap[byte_offset] & (1 << bit_position)) ? 1 : 0;
            
            printf("%d", is_allocated);
            if ((i + 1) % 8 == 0) printf(" "); // Space every 8 bits
            if ((i + 1) % 32 == 0) printf("\n"); // Newline every 32 bits
        }
        printf("\n");
    }
    printf("===========================\n");
}

void debug_print_inodes(int disk_idx) {
    printf("\n=== Allocated Inodes Contents ===\n");
    
    char *inode_region = (char *)disk_map[disk_idx] + super_block.i_blocks_ptr;
    char *inode_bitmap = (char *)disk_map[0] + super_block.i_bitmap_ptr;
    
    for (int i = 0; i < super_block.num_inodes; i++) {
        int byte_offset = i / 8;
        int bit_position = i % 8;
        int is_allocated = (inode_bitmap[byte_offset] & (1 << bit_position)) ? 1 : 0;
        
        if (is_allocated) {
            struct wfs_inode *inode = (struct wfs_inode *)(inode_region + (i * BLOCK_SIZE));
            printf("\nInode %d:\n", i);
            printf("  mode: %d\n", inode->mode);
            printf("  uid: %d\n", inode->uid);
            printf("  gid: %d\n", inode->gid);
            printf("  size: %ld\n", inode->size);
            printf("  nlinks: %d\n", inode->nlinks);
            printf("  blocks[0]: %ld\n", inode->blocks[0]);
            printf("  atime: %ld\n", inode->atim);
            printf("  mtime: %ld\n", inode->mtim);
            printf("  ctime: %ld\n", inode->ctim);
        }
    }
    printf("===========================\n");
}

void debug_print_data_bitmap(){
    printf("\n=== Data Bitmap Contents ===\n");
    
    for (size_t disk = 0; disk < num_disks; disk++) {
        printf("\nDisk %zu:\n", disk);
        char *data_bitmap = (char *)disk_map[disk] + super_block.d_bitmap_ptr;
        
        for (int i = 0; i < super_block.num_data_blocks; i++) {
            int byte_offset = i / 8;
            int bit_position = i % 8;
            int is_allocated = (data_bitmap[byte_offset] & (1 << bit_position)) ? 1 : 0;
            
            printf("%d", is_allocated);
            if ((i + 1) % 8 == 0) printf(" "); // Space every 8 bits
            if ((i + 1) % 32 == 0) printf("\n"); // Newline every 32 bits
        }
        printf("\n");
    }
    printf("===========================\n");
}

void debug_dump_data_regions() {
    printf("\n=== Comparing Data Block Regions ===\n");
    
    // // For each block in data region
    // for (size_t block = 0; block < super_block.num_data_blocks; block++) {
    //     printf("\nBlock %zu:\n", block);
        
    //     // Compare this block across all disks
    //     for (size_t disk = 0; disk < num_disks; disk++) {
    //         printf("Disk %zu: ", disk);
    //         char *block_addr = (char *)disk_map[disk] + super_block.d_blocks_ptr + (block * BLOCK_SIZE);
            
    //         // Print first few bytes
    //         for (int i = 0; i < 16 && i < BLOCK_SIZE; i++) {
    //             printf("%02x ", (unsigned char)block_addr[i]);
    //         }
    //         printf("...\n");
            
    //         // Print as ASCII if printable
    //         printf("ASCII: ");
    //         for (int i = 0; i < 16 && i < BLOCK_SIZE; i++) {
    //             char c = block_addr[i];
    //             printf("%c", (c >= 32 && c <= 126) ? c : '.');
    //         }
    //         printf("...\n");
    //     }
    // }


    printf("\nBlock %zu:\n", (size_t)1);
        // Compare this block across all disks
        for (size_t disk = 0; disk < num_disks; disk++) {
            printf("Disk %zu: ", disk);
            char *block_addr = (char *)disk_map[disk] + super_block.d_blocks_ptr + (1 * BLOCK_SIZE);
            
            // Print first few bytes
            for (int i = 0; i < 16 && i < BLOCK_SIZE; i++) {
                printf("%02x ", (unsigned char)block_addr[i]);
            }
            printf("...\n");
            
            // Print as ASCII if printable
            printf("ASCII: ");
            for (int i = 0; i < 16 && i < BLOCK_SIZE; i++) {
                char c = block_addr[i];
                printf("%c", (c >= 32 && c <= 126) ? c : '.');
            }
            printf("...\n");
        }
    printf("===========================\n");
}




//======================FUSE OPERATIONS===========================//


//get file/directory attributes
//gets inode information and fills stbuf with inode information
int wfs_getattr(const char *path, struct stat *stbuf) {
    printf("getattr called: %s\n", path);
    struct wfs_inode *inode = get_inode(path);
    if (!inode) {
        return -ENOENT;
    }

    //calculate total number of blocks
    int num_blocks = 0;
    for (int i = 0; i < N_BLOCKS; i++)
    {
        if (inode->blocks[i] != 0)
        {
            num_blocks++;
        }
    }

    //Fill stbuf with inode information
    stbuf->st_uid = inode->uid;
    stbuf->st_gid = inode->gid;
    stbuf->st_atime = inode->atim;
    stbuf->st_mtime = inode->mtim;
    stbuf->st_ctime = inode->ctim;
    stbuf->st_mode = inode->mode;
    stbuf->st_size = inode->size;
    stbuf->st_nlink = inode->nlinks;
    stbuf->st_blksize = BLOCK_SIZE;
    stbuf->st_blocks = num_blocks;

    return 0;
}

int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    printf("readdir called: %s\n", path);

    // Get directory inode
    struct wfs_inode *dir_inode = get_inode(path);
    if (!dir_inode) return -ENOENT;
    if (!S_ISDIR(dir_inode->mode)) return -ENOTDIR;

    // Add . and .. entries
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    // Read through directory blocks
    for (int block_idx = 0; block_idx < N_BLOCKS - 1; block_idx++) {
        if (dir_inode->blocks[block_idx] == 0) continue;

        // Get block location based on RAID mode
        size_t disk_idx;
        off_t block_offset;
        
        if (raid_mode == RAID0) {
            disk_idx = get_raid0_disk_index(dir_inode->blocks[block_idx] - 1);
            block_offset = get_raid0_block_offset(dir_inode->blocks[block_idx] - 1);
        } else {
            disk_idx = 0;
            block_offset = super_block.d_blocks_ptr + 
                          ((dir_inode->blocks[block_idx] - 1) * BLOCK_SIZE);
        }

        // Read directory entries
        struct wfs_dentry *entries = (struct wfs_dentry *)((char *)disk_map[disk_idx] + block_offset);
        int entries_per_block = BLOCK_SIZE / sizeof(struct wfs_dentry);
        
        // Fill buffer with valid entries
        for (int i = 0; i < entries_per_block; i++) {
            if (entries[i].num != 0) {  // Valid entry
                // Get entry's inode
                struct wfs_inode *entry_inode = (struct wfs_inode *)((char *)disk_map[0] + 
                    super_block.i_blocks_ptr + (entries[i].num * BLOCK_SIZE));
                
                struct stat st = {0};
                st.st_ino = entries[i].num;
                st.st_mode = entry_inode->mode;
                
                if (filler(buf, entries[i].name, &st, 0))
                    return 0;  // Buffer full
            }
        }
    }
    
    return 0;
}

int wfs_mknod(const char *path, mode_t mode, dev_t rdev) {
    printf("mknod called: %s\n", path);
    //set mode to file
    // mode |= S_IFREG;
    //handle insertion
    int insertion = handle_inode_insertion(path, mode);
    if (insertion != 0){
        return insertion;
    }

    // //print data bitmap 
    debug_print_data_bitmap();
    return 0;//success
}



// creates new inode
// updates inode bitmap
// create new directory entry in parent directory data blocks
// update data bitmap if new data block is allocated
//raid1: update datablocks on all disks and update data bitmap on all disks
//raid0: update datablocks on one disk and update data bitmap on one disk (Which disk to update?)
int wfs_mkdir(const char *path, mode_t mode) {
    printf("mkdir called: %s\n", path);

    //set mode to directory
    mode |= S_IFDIR;

    //handle insertion
    int insertion = handle_inode_insertion(path, mode);
    if (insertion != 0){
        return insertion;
    }
    return 0;//success
}

int wfs_unlink(const char *path) {
    printf("unlink called: %s\n", path);
    
    struct wfs_inode *inode = get_inode(path);
    if (!inode) return -ENOENT;
    if (!S_ISREG(inode->mode)) return -EISDIR;

    char *parent_path = get_parent_path(path);
    char *file_name = get_file_name(path);
    struct wfs_inode *parent = get_inode(parent_path);
    if (!parent) {
        free(parent_path);
        free(file_name);
        return -ENOENT;
    }

    int ret = remove_dir_entry(parent, file_name);
    if (ret < 0) {
        free(parent_path);
        free(file_name);
        return ret;
    }

    free_inode(inode);

    free(parent_path);
    free(file_name);
    return 0;
}

int wfs_rmdir(const char *path) {
    printf("rmdir called: %s\n", path);
    
    // Don't allow removing root
    if (strcmp(path, "/") == 0) {
        return -EBUSY;
    }

    // Get directory inode
    struct wfs_inode *inode = get_inode(path);
    if (!inode) return -ENOENT;
    if (!S_ISDIR(inode->mode)) return -ENOTDIR;
    
    // Check if directory is empty
    if (inode->size > 0) {
        return -ENOTEMPTY;
    }

    // Get parent
    char *parent_path = get_parent_path(path);
    char *dir_name = get_file_name(path);
    struct wfs_inode *parent = get_inode(parent_path);
    if (!parent) {
        free(parent_path);
        free(dir_name);
        return -ENOENT;
    }

    // Remove from parent directory
    int ret = remove_dir_entry(parent, dir_name);
    if (ret < 0) {
        free(parent_path);
        free(dir_name);
        return ret;
    }

    // Update parent's link count (for removed ..)
    parent->nlinks--;

    // Update parent inode on all disks
    for (size_t disk = 0; disk < num_disks; disk++) {
        char *inode_block = (char *)disk_map[disk] + super_block.i_blocks_ptr + 
                           (parent->num * BLOCK_SIZE);
        memcpy(inode_block, parent, sizeof(struct wfs_inode));
    }

    // Free directory's inode and blocks
    free_inode(inode);
    free(parent_path);
    free(dir_name);
    return 0;
}

int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    printf("read called: %s, size=%zu, offset=%ld\n", path, size, offset);
    
    struct wfs_inode *inode = get_inode(path);
    if (!inode) return -ENOENT;
    if (!S_ISREG(inode->mode)) return -EISDIR;

    // Check offset bounds
    if (offset >= inode->size) return 0;
    if (offset + size > inode->size) {
        size = inode->size - offset;
    }

    // Calculate block range
    size_t start_block = offset / BLOCK_SIZE;
    size_t end_block = (offset + size - 1) / BLOCK_SIZE;
    
    // Get indirect block if needed
    off_t *indirect_ptrs = NULL;
    if (end_block >= N_BLOCKS - 1) {
        indirect_ptrs = get_indirect_block(inode);
        if (!indirect_ptrs) return -EIO;
    }

    // Read data blocks
    size_t bytes_read = 0;
    size_t buf_offset = 0;

    for (size_t b = start_block; b <= end_block && bytes_read < size; b++) {
        // Get block number
        off_t block_num;
        if (b < N_BLOCKS - 1) {
            block_num = inode->blocks[b];
        } else {
            block_num = indirect_ptrs[b - (N_BLOCKS - 1)];
        }
        if (block_num == 0) continue;  // Skip unallocated blocks

        // Calculate offsets
        size_t block_offset = (b == start_block) ? offset % BLOCK_SIZE : 0;
        size_t bytes_this_block = BLOCK_SIZE - block_offset;
        if (bytes_read + bytes_this_block > size) {
            bytes_this_block = size - bytes_read;
        }

        // Read block based on RAID mode
        if (raid_mode == RAID0) {
            size_t disk_idx = get_raid0_disk_index(block_num - 1);
            off_t block_addr = get_raid0_block_offset(block_num - 1);
            memcpy(buf + buf_offset, 
                  (char *)disk_map[disk_idx] + block_addr + block_offset,
                  bytes_this_block);
        } else {
            off_t block_addr = super_block.d_blocks_ptr + ((block_num - 1) * BLOCK_SIZE);
            memcpy(buf + buf_offset,
                  (char *)disk_map[0] + block_addr + block_offset,
                  bytes_this_block);
        }

        bytes_read += bytes_this_block;
        buf_offset += bytes_this_block;
    }

    return bytes_read;
}

int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    printf("write called: %s, size=%zu, offset=%ld\n", path, size, offset); 
    // Get file inode
    struct wfs_inode *inode = get_inode(path);
    if (!inode) {
        return -ENOENT;
    }

    // Check if regular file
    if (!S_ISREG(inode->mode)) {
        return -EISDIR;
    }

    // Calculate blocks needed
    size_t end_pos = offset + size;
    size_t start_block = offset / BLOCK_SIZE;

    printf("Start block: %ld\n", start_block);
    size_t end_block = (end_pos + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // Get indirect block if needed
    off_t *indirect_ptrs = NULL;
    if (end_block >= N_BLOCKS - 1) {
        indirect_ptrs = get_indirect_block(inode);
        if (!indirect_ptrs) return -ENOSPC;
    }

    // Write data to blocks
    size_t bytes_written = 0;
    size_t buf_offset = 0;
    
    for (size_t b = start_block; b < end_block && bytes_written < size; b++) {
        // Get block number 
        off_t block_num;
        if (b < N_BLOCKS - 1) { // Direct block
            if (inode->blocks[b] == 0) {
                int new_block = allocate_data_block();
                if (new_block < 0) return -ENOSPC;
                inode->blocks[b] = new_block + 1;
            }
            block_num = inode->blocks[b];
        } else { // Indirect block
            size_t indirect_idx = b - (N_BLOCKS - 1);
            if (indirect_ptrs[indirect_idx] == 0) { // Allocate new indirect block
                int new_block = allocate_data_block();
                if (new_block < 0) return -ENOSPC;
                indirect_ptrs[indirect_idx] = new_block + 1;
            }
            block_num = indirect_ptrs[indirect_idx];
        }
        
        // Calculate offsets within block
        size_t block_offset = (b == start_block) ? offset % BLOCK_SIZE : 0;
        size_t bytes_this_block = BLOCK_SIZE - block_offset;
        if (bytes_written + bytes_this_block > size) {
            bytes_this_block = size - bytes_written;
        }

        if (raid_mode == RAID0) {
            // RAID0: Write to striped disk
            size_t disk_idx = get_raid0_disk_index(block_num - 1);
            off_t block_addr = get_raid0_block_offset(block_num - 1);
            char *block = (char *)disk_map[disk_idx] + block_addr;
            memcpy(block + block_offset, buf + buf_offset, bytes_this_block);

            // Update indirect block if using one
            if (b >= N_BLOCKS - 1) {
                disk_idx = get_raid0_disk_index(inode->blocks[N_BLOCKS-1] - 1);
                block_addr = get_raid0_block_offset(inode->blocks[N_BLOCKS-1] - 1);
                memcpy((char *)disk_map[disk_idx] + block_addr, indirect_ptrs, BLOCK_SIZE);
            }
        } else {
            // RAID1/RAID1V: Write to all disks
            off_t block_addr = super_block.d_blocks_ptr + ((block_num - 1) * BLOCK_SIZE);
            for (size_t disk = 0; disk < num_disks; disk++) {
                char *block = (char *)disk_map[disk] + block_addr;
                memcpy(block + block_offset, buf + bytes_written, bytes_this_block);
                if (b >= N_BLOCKS - 1) {
                    off_t indirect_addr = super_block.d_blocks_ptr + 
                                        ((inode->blocks[N_BLOCKS-1] - 1) * BLOCK_SIZE);
                    memcpy((char *)disk_map[disk] + indirect_addr, indirect_ptrs, BLOCK_SIZE);
                }
            }
        }        
        bytes_written += bytes_this_block;
        buf_offset += bytes_this_block;
    }
    // Update inode metadata
    inode->size = (end_pos > inode->size) ? end_pos : inode->size;
    inode->mtim = inode->ctim = time(NULL);

    // Update inode on all disks
    for (size_t disk = 0; disk < num_disks; disk++) {
        char *inode_block = (char *)disk_map[disk] + super_block.i_blocks_ptr + 
                           (inode->num * BLOCK_SIZE);
        memcpy(inode_block, inode, sizeof(struct wfs_inode));
    }
    debug_print_data_bitmap();
    return bytes_written;
}




//======================MAIN FUNCTION===========================//


// add fuse ops here
static struct fuse_operations ops = {
    .getattr = wfs_getattr,
    .mknod = wfs_mknod,
    .mkdir = wfs_mkdir,
    .unlink = wfs_unlink,
    .rmdir = wfs_rmdir,
    .read = wfs_read,
    .write = wfs_write,
    .readdir = wfs_readdir,
};

// cleanup helper
void cleanup_resources() {
    if (disk_map) {
        free(disk_map);
    }
    if (disk_files) {
        free(disk_files);
    }
}

//Main function 
//Reads disk files, maps them to memory 
int main(int argc, char **argv){
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <disk1> <disk2> [FUSE options] <mount_point>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    //Get disk names from argv and store them in disk_files

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
    if (num_disks < MIN_DISKS) {
        cleanup_resources();
        fprintf(stderr, "Error: At least two disk files are required.\n");
        exit(EXIT_FAILURE);
    }

    //Initialize disk_map with pointers to each disk

    disk_map = malloc(num_disks * sizeof(void *));
    if (!disk_map) {
        cleanup_resources();
        perror("Error allocating memory for disk map");
        exit(EXIT_FAILURE);
    }
    memset(disk_map, 0, num_disks * sizeof(void *)); // Initialize to NULL

    //Map each disk file to memory
    off_t disk_size = 0;
    int current_disk_id = -1;
    for (i = 0; i < num_disks; i++) {
        int fd = open(disk_files[i], O_RDWR);
        if (fd < 0) {
            cleanup_resources();
            perror("Error opening disk file");
            exit(EXIT_FAILURE);
        }
        struct stat stat;
        fstat(fd, &stat);

        disk_size = stat.st_size; //size of disk file

        //get superblock from disk file
        struct wfs_sb sb_temp;
        void *disk_ptr = mmap(NULL, disk_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (disk_ptr == MAP_FAILED) {
            perror("Error mapping disk file");
            cleanup_resources();
            exit(EXIT_FAILURE);
        }
        memcpy(&sb_temp, disk_ptr, sizeof(struct wfs_sb));
        

        //Store disk pointer in disk_map based on disk_id

        current_disk_id = sb_temp.disk_id;
        if (current_disk_id >= num_disks || current_disk_id < 0) {
            fprintf(stderr, "Invalid disk_id %d\n", current_disk_id);
            cleanup_resources();
            exit(EXIT_FAILURE);
        }

        disk_map[current_disk_id] = disk_ptr;
        if(disk_map[current_disk_id] == MAP_FAILED) {
            perror("Error mapping disk file");
            cleanup_resources();
            exit(EXIT_FAILURE);
        }
        close(fd);
    }

    //store dirst superblock for reference
    super_block = *(struct wfs_sb *)disk_map[0];
    //set raid mode
    raid_mode = super_block.raid_mode;   

    //print inode bitmap and inodes
    //debug_print_inode_bitmap();
    //debug_print_data_bitmap();
    printf("WFS starting...\n");
    return fuse_main(argc-(num_disks + 1), &argv[num_disks+ 1], &ops, NULL);
}
