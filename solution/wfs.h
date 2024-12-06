#ifndef WFS_H
#define WFS_H


#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>


#define BLOCK_SIZE (512)
#define MAX_NAME   (28)

#define D_BLOCK    (6)
#define IND_BLOCK  (D_BLOCK+1)
#define N_BLOCKS   (IND_BLOCK+1)

/*
  The fields in the superblock should reflect the structure of the filesystem.
  `mkfs` writes the superblock to offset 0 of the disk image. 
  The disk image will have this format:

          d_bitmap_ptr       d_blocks_ptr
               v                  v
+----+---------+---------+--------+--------------------------+
| SB | IBITMAP | DBITMAP | INODES |       DATA BLOCKS        |
+----+---------+---------+--------+--------------------------+
0    ^                   ^
i_bitmap_ptr        i_blocks_ptr

*/

// Superblock
struct wfs_sb {
    size_t num_inodes;
    size_t num_data_blocks;
    off_t i_bitmap_ptr;
    off_t d_bitmap_ptr;
    off_t i_blocks_ptr;
    off_t d_blocks_ptr;
    // Extend after this line
};

// Inode
struct wfs_inode {
    int     num;      /* Inode number */
    mode_t  mode;     /* File type and mode */
    uid_t   uid;      /* User ID of owner */
    gid_t   gid;      /* Group ID of owner */
    off_t   size;     /* Total size, in bytes */
    int     nlinks;   /* Number of links */

    time_t atim;      /* Time of last access */
    time_t mtim;      /* Time of last modification */
    time_t ctim;      /* Time of last status change */

    off_t blocks[N_BLOCKS];
};

// Directory entry
struct wfs_dentry {
    char name[MAX_NAME];
    int num;
};

// // Function prototypes for FUSE operations
// int wfs_getattr(const char *path, struct stat *stbuf);
// int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
// int wfs_mknod(const char *path, mode_t mode, dev_t rdev);
// int wfs_mkdir(const char *path, mode_t mode);
// int wfs_unlink(const char *path);
// int wfs_rmdir(const char *path);
// int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
// int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);

#endif // WFS_H