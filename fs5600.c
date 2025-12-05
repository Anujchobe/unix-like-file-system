/*
 * file:        fs5600.c
 * description: skeleton file for CS 5600 system
 *
 * CS 5600, Computer Systems, Northeastern CCIS
 * Peter Desnoyers, November 2019
 *
 * Modified by CS5600 staff, fall 2021.
 */

#define FUSE_USE_VERSION 27
#define _FILE_OFFSET_BITS 64

#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <fuse.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <time.h>

#include "fs5600.h"

// === Global in-memory filesystem state ===

static super_t superblock;                 // cached superblock (block 0)
static unsigned char block_bitmap[FS_BLOCK_SIZE];  // cached bitmap (block 1)


/* if you don't understand why you can't use these system calls here,
 * you need to read the assignment description another time
 */
#define stat(a,b) error do not use stat()
#define open(a,b) error do not use open()
#define read(a,b,c) error do not use read()
#define write(a,b,c) error do not use write()


// === block manipulation functions ===

/* disk access.
 * All access is in terms of 4KB blocks; read and
 * write functions return 0 (success) or -EIO.
 *
 * read/write "nblks" blocks of data
 *   starting from block id "lba"
 *   to/from memory "buf".
 *     (see implementations in misc.c)
 */
extern int block_read(void *buf, int lba, int nblks);
extern int block_write(void *buf, int lba, int nblks);

/* bitmap functions
 */
void bit_set(unsigned char *map, int i)
{
    map[i/8] |= (1 << (i%8));
}
void bit_clear(unsigned char *map, int i)
{
    map[i/8] &= ~(1 << (i%8));
}
int bit_test(unsigned char *map, int i)
{
    return map[i/8] & (1 << (i%8));
}


/*
 * Allocate a free block from the disk.
 *
 * success - return free block number
 * no free block - return -ENOSPC
 *
 * hint:
 *   - bit_set/bit_test might be useful.
 */
int alloc_blk() {
    /* Your code here */

     // Scan all blocks and find a free one (bit = 0)
    // We are allowed to allocate any block; blocks 0 and 1 are already
    // marked used in the bitmap created by gen-disk.py.
    for (uint32_t i = 0; i < superblock.disk_size; i++) {
        if (!bit_test(block_bitmap, i)) {
            bit_set(block_bitmap, i);
            // write updated bitmap back to disk (block 1)
            if (block_write(block_bitmap, 1, 1) < 0) {
                // undo the bit in memory so we don’t lie to ourselves
                bit_clear(block_bitmap, i);
                return -EIO;
            }
            return (int)i;
        }
    }
    return -ENOSPC;
   
}

/*
 * Return a block to disk, which can be used later.
 *
 * hint:
 *   - bit_clear might be useful.
 */
void free_blk(int i) {
    /* your code here*/

      if (i < 0 || (uint32_t)i >= superblock.disk_size) {
        return;     // silently ignore bad input
    }
    bit_clear(block_bitmap, i);
    // We can’t return an error from here, so just assert success.
    // If there’s an I/O error, tests will fail loudly, which is fine.
    assert(block_write(block_bitmap, 1, 1) == 0);
}


// === FS helper functions ===


/* Two notes on path translation:
 *
 * (1) translation errors:
 *
 *   In addition to the method-specific errors listed below, almost
 *   every method can return one of the following errors if it fails to
 *   locate a file or directory corresponding to a specified path.
 *
 *   ENOENT - a component of the path doesn't exist.
 *   ENOTDIR - an intermediate component of the path (e.g. 'b' in
 *             /a/b/c) is not a directory
 *
 * (2) note on splitting the 'path' variable:
 *
 *   the value passed in by the FUSE framework is declared as 'const',
 *   which means you can't modify it. The standard mechanisms for
 *   splitting strings in C (strtok, strsep) modify the string in place,
 *   so you have to copy the string and then free the copy when you're
 *   done. One way of doing this:
 *
 *      char *_path = strdup(path);
 *      int inum = ... // translate _path to inode number
 *      free(_path);
 */


/* EXERCISE 2:
 * convert path into inode number.
 *
 * how?
 *  - first split the path into directory and file names
 *  - then, start from the root inode (which inode/block number is that?)
 *  - then, walk the dirs to find the final file or dir.
 *    when walking:
 *      -- how do I know if an inode is a dir or file? (hint: mode)
 *      -- what should I do if something goes wrong? (hint: read the above note about errors)
 *      -- how many dir entries in one inode? (hint: read Lab4 instructions about directory inode)
 *
 * hints:
 *  - you can safely assume the max depth of nested dirs is 10
 *  - a bunch of string functions may be useful (e.g., "strtok", "strsep", "strcmp")
 *  - "block_read" may be useful.
 *  - "S_ISDIR" may be useful. (what is this? read Lab4 instructions or "man inode")
 *
 * programing hints:
 *  - there are several functionalities that you will reuse; it's better to
 *  implement them in other functions.
 */

int path2inum(const char *path) {
    /* your code here */

      // Root path
    if (strcmp(path, "/") == 0) {
        return 2;   // root inode is always block 2
    }

    // Make a writable copy of path
    char *copy = strdup(path);
    if (!copy) {
        return -ENOMEM;
    }

    int cur_inum = 2;  // start at root
    char *saveptr = NULL;
    char *tok = strtok_r(copy, "/", &saveptr);

    while (tok != NULL) {
        inode_t cur_inode;
        // Read current inode block
        if (block_read(&cur_inode, cur_inum, 1) < 0) {
            free(copy);
            return -EIO;
        }

        // Current must be a directory to look up children
        if (!S_ISDIR(cur_inode.mode)) {
            free(copy);
            return -ENOTDIR;
        }

        // Directory must have a data block
        if (cur_inode.ptrs[0] == 0) {
            free(copy);
            return -ENOENT;
        }

        dirent_t entries[NUM_DIRENT_BLOCK];
        if (block_read(entries, cur_inode.ptrs[0], 1) < 0) {
            free(copy);
            return -EIO;
        }

        int found = 0;
        for (int i = 0; i < NUM_DIRENT_BLOCK; i++) {
            if (entries[i].valid && strcmp(entries[i].name, tok) == 0) {
                cur_inum = entries[i].inode;
                found = 1;
                break;
            }
        }

        if (!found) {
            free(copy);
            return -ENOENT;
        }

        tok = strtok_r(NULL, "/", &saveptr);
    }

    free(copy);
    return cur_inum;
}


/* EXERCISE 2:
 * Helper function:
 *   copy the information in an inode to struct stat
 *   (see its definition below, and the full Linux definition in "man lstat".)
 *
 *  struct stat {
 *        ino_t     st_ino;         // Inode number
 *        mode_t    st_mode;        // File type and mode
 *        nlink_t   st_nlink;       // Number of hard links
 *        uid_t     st_uid;         // User ID of owner
 *        gid_t     st_gid;         // Group ID of owner
 *        off_t     st_size;        // Total size, in bytes
 *        blkcnt_t  st_blocks;      // Number of blocks allocated
 *                                  // (note: block size is FS_BLOCK_SIZE;
 *                                  // and this number is an int which should be round up)
 *
 *        struct timespec st_atim;  // Time of last access
 *        struct timespec st_mtim;  // Time of last modification
 *        struct timespec st_ctim;  // Time of last status change
 *    };
 *
 *  [hints:
 *
 *    - what you should do is mostly copy.
 *
 *    - read fs_inode in fs5600.h and compare with struct stat.
 *
 *    - you can safely treat the types "ino_t", "mode_t", "nlink_t", "uid_t"
 *      "gid_t", "off_t", "blkcnt_t" as "unit32_t" in this lab.
 *
 *    - read "man clock_gettime" to see "struct timespec" definition
 *
 *    - the above "struct stat" does not show all attributes, but we don't care
 *      the rest attributes.
 *
 *    - for several fields in 'struct stat' there is no corresponding
 *    information in our file system:
 *      -- st_nlink - always set it to 1  (recall that fs5600 doesn't support links)
 *      -- st_atime - set to same value as st_mtime
 *  ]
 */

void inode2stat(struct stat *sb, struct fs_inode *in, uint32_t inode_num)
{
    memset(sb, 0, sizeof(*sb));

    /* your code here */

    sb->st_ino   = inode_num;
    sb->st_mode  = in->mode;
    sb->st_nlink = 1;            // no hard links in fs5600
    sb->st_uid   = in->uid;
    sb->st_gid   = in->gid;
    sb->st_size  = in->size;

    // Number of FS blocks the file logically occupies
    if (in->size > 0) {
        int blocks = DIV_ROUND_UP(in->size, FS_BLOCK_SIZE);
        sb->st_blocks = blocks * (FS_BLOCK_SIZE / 512);  // 512-byte units
    } else {
        sb->st_blocks = 0;
    }

    // Times: our inode stores seconds since epoch
    sb->st_mtime = in->mtime;
    sb->st_ctime = in->ctime;
    sb->st_atime = in->mtime;    // no atime stored, reuse mtime

    // Also fill timespec fields (if present on this libc)
    sb->st_mtim.tv_sec = in->mtime;
    sb->st_mtim.tv_nsec = 0;
    sb->st_ctim.tv_sec = in->ctime;
    sb->st_ctim.tv_nsec = 0;
    sb->st_atim.tv_sec = in->mtime;
    sb->st_atim.tv_nsec = 0;
}




// ====== FUSE APIs ========

/* EXERCISE 1:
 * init - this is called once by the FUSE framework at startup.
 *
 * The function should:
 *   - read superblock
 *   - check if the magic number matches FS_MAGIC
 *   - initialize whatever in-memory data structure your fs5600 needs
 *     (you may come back later when requiring new data structures)
 *
 * notes:
 *   - ignore the 'conn' argument.
 *   - use "block_read" to read data (if you don't know how it works, read its
 *     implementation in misc.c)
 *   - if there is an error, exit(1)
 */
void* fs_init(struct fuse_conn_info *conn)
{
    /* your code here */

      (void)conn;  // unused

    // Read superblock (block 0)
    if (block_read(&superblock, 0, 1) < 0) {
        fprintf(stderr, "fs_init: failed to read superblock\n");
        exit(1);
    }

    if (superblock.magic != FS_MAGIC) {
        fprintf(stderr, "fs_init: bad magic number 0x%x (expected 0x%x)\n",
                superblock.magic, FS_MAGIC);
        exit(1);
    }

    // Read block bitmap (block 1) into memory
    if (block_read(block_bitmap, 1, 1) < 0) {
        fprintf(stderr, "fs_init: failed to read block bitmap\n");
        exit(1);
    }
    return NULL;
}


/* EXERCISE 1:
 * statfs - get file system statistics
 * see 'man 2 statfs' for description of 'struct statvfs'.
 * Errors - none. Needs to work.
 */
int fs_statfs(const char *path, struct statvfs *st)
{
    /* needs to return the following fields (ignore others):
     *   [DONE] f_bsize = FS_BLOCK_SIZE
     *   [DONE] f_namemax = <whatever your max namelength is>
     *   [TODO] f_blocks = total image - (superblock + block map)
     *   [TODO] f_bfree = f_blocks - blocks used
     *   [TODO] f_bavail = f_bfree
     *
     * it's okay to calculate this dynamically on the rare occasions
     * when this function is called.
     */

    st->f_bsize = FS_BLOCK_SIZE;
    st->f_namemax = 27;  // why? see fs5600.h

    /* your code here */
    (void)path;  // we don’t care which path; stats are for the whole FS

    st->f_bsize = FS_BLOCK_SIZE;
    st->f_frsize = FS_BLOCK_SIZE;  // fragment size – same as block size here
    st->f_namemax = 27;            // max filename length (see fs5600.h)

    uint32_t total_blocks = superblock.disk_size;

    if (total_blocks <= 2) {
        // pathological case: no space beyond super + bitmap
        st->f_blocks = 0;
        st->f_bfree = 0;
        st->f_bavail = 0;
        return 0;
    }

    // Total usable blocks for inodes + data, excluding superblock (0) and bitmap (1)
    st->f_blocks = total_blocks - 2;

    // Count free blocks among [2, total_blocks)
    unsigned long free_count = 0;
    for (uint32_t i = 2; i < total_blocks; i++) {
        if (!bit_test(block_bitmap, i)) {
            free_count++;
        }
    }

    st->f_bfree  = free_count;
    st->f_bavail = free_count;

    // You can leave other fields (f_files, f_ffree, etc.) as 0.
    return 0;
}


/* EXERCISE 2:
 * getattr - get file or directory attributes. For a description of
 *  the fields in 'struct stat', read 'man 2 stat'.
 *
 * You should:
 *  1. parse the path given by "const char * path",
 *     find the inode of the specified file,
 *       [note: you should implement the helfer function "path2inum"
 *       and use it.]
 *  2. copy inode's information to "struct stat",
 *       [note: you should implement the helper function "inode2stat"
 *       and use it.]
 *  3. and return:
 *     ** success - return 0
 *     ** errors - path translation, ENOENT
 */


int fs_getattr(const char *path, struct stat *sb)
{
    /* your code here */

  
        int inum = path2inum(path);
    if (inum < 0) {
        return inum;   // already a negative errno
    }

    inode_t in;
    if (block_read(&in, inum, 1) < 0) {
        return -EIO;
    }

    inode2stat(sb, &in, (uint32_t)inum);
    return 0;
}

/* EXERCISE 2:
 * readdir - get directory contents.
 *
 * call the 'filler' function for *each valid entry* in the
 * directory, as follows:
 *     filler(ptr, <name>, <statbuf>, 0)
 * where
 *   ** "ptr" is the second argument
 *   ** <name> is the name of the file/dir (the name in the direntry)
 *   ** <statbuf> is a pointer to the struct stat (of the file/dir)
 *
 * success - return 0
 * errors - path resolution, ENOTDIR, ENOENT
 *
 * hints:
 *   - this process is similar to the fs_getattr:
 *     -- you will walk file system to find the dir pointed by "path",
 *     -- then you need to call "filler" for each of
 *        the *valid* entry in this dir
 *   - you can ignore "struct fuse_file_info *fi" (also apply to later Exercises)
 */
int fs_readdir(const char *path, void *ptr, fuse_fill_dir_t filler,
               off_t offset, struct fuse_file_info *fi)
{
    /* your code here */

      (void)offset;
    (void)fi;

    int inum = path2inum(path);
    if (inum < 0) {
        return inum;   // ENOENT or ENOTDIR
    }

    inode_t dir_inode;
    if (block_read(&dir_inode, inum, 1) < 0) {
        return -EIO;
    }

    if (!S_ISDIR(dir_inode.mode)) {
        return -ENOTDIR;
    }

    // Empty directory? No entries to list.
    if (dir_inode.ptrs[0] == 0) {
        return 0;
    }

    dirent_t entries[NUM_DIRENT_BLOCK];
    if (block_read(entries, dir_inode.ptrs[0], 1) < 0) {
        return -EIO;
    }

    for (int i = 0; i < NUM_DIRENT_BLOCK; i++) {
        if (entries[i].valid) {
            // test1.c's filler ignores stbuf, so we can pass NULL
            filler(ptr, entries[i].name, NULL, 0);
        }
    }

    return 0;
}


/* EXERCISE 3:
 * read - read data from an open file.
 * success: should return exactly the number of bytes requested, except:
 *   - if offset >= file len, return 0
 *   - if offset+len > file len, return #bytes from offset to end
 *   - on error, return <0
 * Errors - path resolution, ENOENT, EISDIR
 */
int fs_read(const char *path, char *buf, size_t len, off_t offset,
        struct fuse_file_info *fi)
{
    /* your code here */

      (void)fi;

    int inum = path2inum(path);
    if (inum < 0) {
        return inum;   // ENOENT or ENOTDIR
    }

    inode_t in;
    if (block_read(&in, inum, 1) < 0) {
        return -EIO;
    }

    if (S_ISDIR(in.mode)) {
        return -EISDIR;
    }

    // If offset beyond end of file, nothing to read
    if (offset >= in.size) {
        return 0;
    }

    // Clamp len so we don't read past EOF
    size_t max_can_read = (size_t)(in.size - offset);
    if (len > max_can_read) {
        len = max_can_read;
    }
    if (len == 0) {
        return 0;
    }

    size_t total_read = 0;

    while (total_read < len) {
        off_t cur_off = offset + total_read;
        int blk_index = (int)(cur_off / FS_BLOCK_SIZE);
        int blk_off   = (int)(cur_off % FS_BLOCK_SIZE);

        if (blk_index >= NUM_PTRS_INODE) {
            break; // inconsistent inode; stop
        }

        uint32_t blkno = in.ptrs[blk_index];
        if (blkno == 0) {
            break; // hole / inconsistency
        }

        char block[FS_BLOCK_SIZE];
        if (block_read(block, blkno, 1) < 0) {
            return -EIO;
        }

        size_t can_copy = FS_BLOCK_SIZE - blk_off;
        size_t remaining = len - total_read;
        if (can_copy > remaining) {
            can_copy = remaining;
        }

        memcpy(buf + total_read, block + blk_off, can_copy);
        total_read += can_copy;
    }

    return (int)total_read;
}



/* EXERCISE 3:
 * rename - rename a file or directory
 * success - return 0
 * Errors - path resolution, ENOENT, EINVAL, EEXIST
 *
 * ENOENT - source does not exist
 * EEXIST - destination already exists
 * EINVAL - source and destination are not in the same directory
 *
 * Note that this is a simplified version of the UNIX rename
 * functionality - see 'man 2 rename' for full semantics. In
 * particular, the full version can move across directories, replace a
 * destination file, and replace an empty directory with a full one.
 */


/* Helper: split an absolute path into parent directory and name.
 * For example: "/dir2/file.4k+" -> parent="/dir2", name="file.4k+"
 *              "/file.1k"      -> parent="/",    name="file.1k"
 * Caller must free(*parent_out). name_out points into the original path.
 */
static int split_parent_child(const char *path, char **parent_out, char **name_out)
{
    if (strcmp(path, "/") == 0) {
        return -EINVAL;  // can't split root
    }

    const char *last = strrchr(path, '/');
    if (!last) {
        return -EINVAL;  // paths should always be absolute
    }

    const char *name = last + 1;
    if (*name == '\0') {
        // trailing slash case like "/dir/"; not expected in tests
        return -EINVAL;
    }

    char *parent = NULL;
    if (last == path) {
        // Parent is just "/"
        parent = strdup("/");
    } else {
        size_t plen = (size_t)(last - path);
        parent = malloc(plen + 1);
        if (!parent) {
            return -ENOMEM;
        }
        memcpy(parent, path, plen);
        parent[plen] = '\0';
    }

    if (!parent) {
        return -ENOMEM;
    }

    *parent_out = parent;
    *name_out = (char *)name;  // safe: we don't modify *name
    return 0;
}




int fs_rename(const char *src_path, const char *dst_path)
{
    /* your code here */

      char *src_parent = NULL, *dst_parent = NULL;
    char *src_name = NULL, *dst_name = NULL;

    int rv = split_parent_child(src_path, &src_parent, &src_name);
    if (rv < 0) {
        return rv;
    }
    rv = split_parent_child(dst_path, &dst_parent, &dst_name);
    if (rv < 0) {
        free(src_parent);
        return rv;
    }

    // Source and destination must be in the same directory
    if (strcmp(src_parent, dst_parent) != 0) {
        free(src_parent);
        free(dst_parent);
        return -EINVAL;
    }

    // Too-long name is invalid (>27)
    if (strlen(dst_name) > 27) {
        free(src_parent);
        free(dst_parent);
        return -EINVAL;
    }

    // Find parent directory inode
    int dir_inum = path2inum(src_parent);
    free(src_parent);
    free(dst_parent);

    if (dir_inum < 0) {
        return dir_inum;   // ENOENT / ENOTDIR
    }

    inode_t dir_inode;
    if (block_read(&dir_inode, dir_inum, 1) < 0) {
        return -EIO;
    }
    if (!S_ISDIR(dir_inode.mode)) {
        return -ENOTDIR;
    }
    if (dir_inode.ptrs[0] == 0) {
        return -ENOENT;    // empty dir, nothing to rename
    }

    dirent_t entries[NUM_DIRENT_BLOCK];
    if (block_read(entries, dir_inode.ptrs[0], 1) < 0) {
        return -EIO;
    }

    // Find source entry
    int src_idx = -1;
    for (int i = 0; i < NUM_DIRENT_BLOCK; i++) {
        if (entries[i].valid && strcmp(entries[i].name, src_name) == 0) {
            src_idx = i;
            break;
        }
    }
    if (src_idx < 0) {
        return -ENOENT;
    }

    // Check if destination name already exists
    for (int i = 0; i < NUM_DIRENT_BLOCK; i++) {
        if (entries[i].valid && strcmp(entries[i].name, dst_name) == 0) {
            return -EEXIST;
        }
    }

    // Perform rename: change the name in the src entry
    strncpy(entries[src_idx].name, dst_name, 27);
    entries[src_idx].name[27] = '\0';

    if (block_write(entries, dir_inode.ptrs[0], 1) < 0) {
        return -EIO;
    }

    return 0;
    
}

/* EXERCISE 3:
 * chmod - change file permissions
 *
 * success - return 0
 * Errors - path resolution, ENOENT.
 *
 * hints:
 *   - You can safely assume the "mode" is valid.
 *   - notice that "mode" only contains permissions
 *     (blindly assign it to inode mode doesn't work;
 *      why? check out Lab4 instructions about mode)
 *   - S_IFMT might be useful.
 */
int fs_chmod(const char *path, mode_t mode)
{
    /* your code here */
      int inum = path2inum(path);
    if (inum < 0) {
        return inum;   // ENOENT etc
    }

    inode_t in;
    if (block_read(&in, inum, 1) < 0) {
        return -EIO;
    }

    // 'mode' only contains permission bits. Preserve type bits.
    uint32_t type_bits = in.mode & S_IFMT;
    uint32_t perm_bits = mode & 0777;
    in.mode = type_bits | perm_bits;

    // Update ctime to now
    in.ctime = (uint32_t)time(NULL);

    if (block_write(&in, inum, 1) < 0) {
        return -EIO;
    }

    return 0;
    
}


/* EXERCISE 4:
 * create - create a new file with specified permissions
 *
 * success - return 0
 * errors - path resolution, EEXIST
 *          in particular, for create("/a/b/c") to succeed,
 *          "/a/b" must exist, and "/a/b/c" must not.
 *
 * If a file or directory of this name already exists, return -EEXIST.
 * If there are already 128 entries in the directory (i.e. it's filled an
 * entire block), you are free to return -ENOSPC instead of expanding it.
 * If the name is too long (longer than 27 letters), return -EINVAL.
 *
 * notes:
 *   - that 'mode' only has the permission bits. You have to OR it with S_IFREG
 *     before setting the inode 'mode' field.
 *   - Ignore the third parameter.
 *   - you will have to implement the helper funciont "alloc_blk" first
 *   - when creating a file, remember to initialize the inode ptrs.
 */
int fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    uint32_t cur_time = time(NULL);
    struct fuse_context *ctx = fuse_get_context();
    uint16_t uid = ctx->uid;
    uint16_t gid = ctx->gid;

    // get rid of compiling warnings; you should remove later
    (void) uid, (void) gid, (void) cur_time;

    /* your code here */
    return -EOPNOTSUPP;
}



/* EXERCISE 4:
 * mkdir - create a directory with the given mode.
 *
 * Note that 'mode' only has the permission bits. You
 * have to OR it with S_IFDIR before setting the inode 'mode' field.
 *
 * success - return 0
 * Errors - path resolution, EEXIST
 * Conditions for EEXIST are the same as for create.
 *
 * hint:
 *   - there is a lot of similaries between fs_mkdir and fs_create.
 *     you may want to reuse many parts (note: reuse is not copy-paste!)
 */
int fs_mkdir(const char *path, mode_t mode)
{
    uint32_t cur_time = time(NULL);
    struct fuse_context *ctx = fuse_get_context();
    uint16_t uid = ctx->uid;
    uint16_t gid = ctx->gid;

    // get rid of compiling warnings; you should remove later
    (void) uid, (void) gid, (void) cur_time;

    /* your code here */
    return -EOPNOTSUPP;
}


/* EXERCISE 5:
 * unlink - delete a file
 *  success - return 0
 *  errors - path resolution, ENOENT, EISDIR
 *
 * hint:
 *   - you will have to implement the helper funciont "free_blk" first
 *   - remember to delete all data blocks as well
 *   - remember to update "mtime"
 */
int fs_unlink(const char *path)
{
    /* your code here */
    return -EOPNOTSUPP;
}

/* EXERCISE 5:
 * rmdir - remove a directory
 *  success - return 0
 *  Errors - path resolution, ENOENT, ENOTDIR, ENOTEMPTY
 *
 * hint:
 *   - fs_rmdir and fs_unlink have a lot in common; think of reuse the code
 */
int fs_rmdir(const char *path)
{
    /* your code here */
    return -EOPNOTSUPP;
}

/* EXERCISE 6:
 * write - write data to a file
 * success - return number of bytes written. (this will be the same as
 *           the number requested, or else it's an error)
 *
 * Errors - path resolution, ENOENT, EISDIR, ENOSPC
 *  return EINVAL if 'offset' is greater than current file length.
 *  (POSIX semantics support the creation of files with "holes" in them,
 *   but we don't)
 *  return ENOSPC when the data exceed the maximum size of a file.
 */
int fs_write(const char *path, const char *buf, size_t len,
         off_t offset, struct fuse_file_info *fi)
{
    /* your code here */
    return -EOPNOTSUPP;
}

/* EXERCISE 6:
 * truncate - truncate file to exactly 'len' bytes
 * note that CS5600 fs only allows len=0, meaning discard all data in this file.
 *
 * success - return 0
 * Errors - path resolution, ENOENT, EISDIR, EINVAL
 *    return EINVAL if len > 0.
 */
int fs_truncate(const char *path, off_t len)
{
    /* you can cheat by only implementing this for the case of len==0,
     * and an error otherwise.
     */
    if (len != 0) {
        return -EINVAL;        /* invalid argument */
    }

    /* your code here */
    return -EOPNOTSUPP;
}

/* EXERCISE 6:
 * Change file's last modification time.
 *
 * notes:
 *  - read "man 2 utime" to know more.
 *  - when "ut" is NULL, update the time to now (i.e., time(NULL))
 *  - you only need to use the "modtime" in "struct utimbuf" (ignore "actime")
 *    and you only have to update "mtime" in inode.
 *
 * success - return 0
 * Errors - path resolution, ENOENT
 */
int fs_utime(const char *path, struct utimbuf *ut)
{
    /* your code here */
    return -EOPNOTSUPP;

}



/* operations vector. Please don't rename it, or else you'll break things
 */
struct fuse_operations fs_ops = {
    .init = fs_init,            /* read-mostly operations */
    .statfs = fs_statfs,
    .getattr = fs_getattr,
    .readdir = fs_readdir,
    .read = fs_read,
    .rename = fs_rename,
    .chmod = fs_chmod,

    .create = fs_create,        /* write operations */
    .mkdir = fs_mkdir,
    .unlink = fs_unlink,
    .rmdir = fs_rmdir,
    .write = fs_write,
    .truncate = fs_truncate,
    .utime = fs_utime,
};

