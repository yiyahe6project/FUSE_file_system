/*
 * file:        homework.c
 * description: skeleton file for CS 5600 file system
 *
 * CS 5600, Computer Systems, Northeastern CCIS
 * Peter Desnoyers, Fall 2020
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
#include "fs5600.h"

/* if you don't understand why you can't use these system calls here, 
 * you need to read the assignment description another time
 */
#define stat(a,b) error do not use stat()
#define open(a,b) error do not use open()
#define read(a,b,c) error do not use read()
#define write(a,b,c) error do not use write()

#define MAX_PATH_LEN 10
#define MAX_NAME_LEN 27

/* disk access. All access is in terms of 4KB blocks; read and
 * write functions return 0 (success) or -EIO.
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


/* init - this is called once by the FUSE framework at startup. Ignore
 * the 'conn' argument.
 * recommended actions:
 *   - read superblock
 *   - allocate memory, block allocation bitmap
 */

struct fs_super super;
unsigned char bitmap[FS_BLOCK_SIZE];

void* fs_init(struct fuse_conn_info *conn)
{
    /* your code here */
 
    block_read(&super, 0, 1);
    block_read(bitmap, 1, 1);

    return NULL;
}

static void set_attr(struct fs_inode *inode, struct stat *sb){
    memset(sb, 0, sizeof(*sb));
    sb->st_uid = inode->uid;
    sb->st_gid = inode->gid;
    sb->st_mode = inode->mode;
    sb->st_atime = inode->mtime;
    sb->st_ctime = inode->ctime;
    sb->st_mtime = inode->mtime;
    sb->st_size = inode->size;
    sb->st_blksize = FS_BLOCK_SIZE;
    sb->st_nlink = 1;
    sb->st_blocks = (inode->size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
}

/* Note on path translation errors:
 * In addition to the method-specific errors listed below, almost
 * every method can return one of the following errors if it fails to
 * locate a file or directory corresponding to a specified path.
 *
 * ENOENT - a component of the path doesn't exist.
 * ENOTDIR - an intermediate component of the path (e.g. 'b' in
 *           /a/b/c) is not a directory
 */

/* note on splitting the 'path' variable:
 * the value passed in by the FUSE framework is declared as 'const',
 * which means you can't modify it. The standard mechanisms for
 * splitting strings in C (strtok, strsep) modify the string in place,
 * so you have to copy the string and then free the copy when you're
 * done. One way of doing this:
 *
 *    char *_path = strdup(path);
 *    int inum = translate(_path);
 *    free(_path);
 */


/* parse - split path into tokens 
 * return count/array of tokens
 */
int parse(char *path, char **argv)
{
    int i;
    for (i = 0; i < MAX_PATH_LEN; i++) {
        if ((argv[i] = strtok(path, "/")) == NULL)
            break;
        if (strlen(argv[i]) > MAX_NAME_LEN)
            argv[i][MAX_NAME_LEN] = 0;        // truncate to 27 characters
        path = NULL;
    }
    return i;
}


/* search - search specific file/dir given inum and file/dir name
*  return 0 if not found else inum
*/
int search(int inum, char *name) {
    int found = 0;
    struct fs_inode *inode = malloc(sizeof(*inode));
    block_read(inode, inum, 1);
    struct fs_dirent entries[DIRECTORY_ENTS_PER_BLK];
    block_read(entries, inode->ptrs[0], 1);
    for (int j = 0; j < DIRECTORY_ENTS_PER_BLK; j++) {
        if (entries[j].valid && strcmp(entries[j].name, name) == 0) {
            inum = entries[j].inode;
            free(inode);
            return inum;
        }
    }
    free(inode);
    return found;
}

/* translate - given path token array and count
 *             return inum if found, else return error
 * errors -ENOTDIR: the intermediate of path is not a directory
 *        -ENOENT: component of path is not found
 */
int translate(int pathc, char **pathv) {
    int inum = 2; // alway start from root 
    struct fs_inode *inode = malloc(sizeof(*inode));
    for (int i = 0; i < pathc; i++) {
        block_read(inode, inum, 1);
        if (!S_ISDIR(inode->mode)) {
            free(inode);
            return -ENOTDIR;
        }
        inum = search(inum, pathv[i]);
        if (inum == 0) {
            free(inode);
            return -ENOENT;
        }
    }
    free(inode);
    return inum;
}


/* getattr - get file or directory attributes. For a description of
 *  the fields in 'struct stat', see 'man lstat'.
 *
 * Note - for several fields in 'struct stat' there is no corresponding
 *  information in our file system:
 *    st_nlink - always set it to 1
 *    st_atime, st_ctime - set to same value as st_mtime
 *
 * success - return 0
 * errors - path translation, ENOENT
 * hint - factor out inode-to-struct stat conversion - you'll use it
 *        again in readdir
 */

int fs_getattr(const char *path, struct stat *sb)
{
    char *_path = strdup(path);
    char *pathv[MAX_NAME_LEN];
    int pathc = parse(_path, pathv);
    int inum = translate(pathc, pathv);
    free(_path);
    if (inum == -ENOENT || inum == -ENOTDIR) {
    	return inum;
    }

    struct fs_inode *inode = malloc(sizeof(*inode));
    block_read(inode, inum, 1);
    set_attr(inode, sb);

    free(inode);
    return 0;
}

/* readdir - get directory contents.
 *
 * call the 'filler' function once for each valid entry in the 
 * directory, as follows:
 *     filler(buf, <name>, <statbuf>, 0)
 * where <statbuf> is a pointer to a struct stat
 * success - return 0
 * errors - path resolution, ENOTDIR, ENOENT
 * 
 * hint - check the testing instructions if you don't understand how
 *        to call the filler function
 */
int fs_readdir(const char *path, void *ptr, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
    char *_path = strdup(path);
    char *pathv[MAX_NAME_LEN];
    int pathc = parse(_path, pathv);
    int inum = translate(pathc, pathv);
    free(_path);
    if (inum == -ENOENT || inum == -ENOTDIR) {
    	return inum;
    }

    struct fs_inode *inode = malloc(sizeof(*inode));
    block_read(inode, inum, 1);
    if (!S_ISDIR(inode->mode)) {
        free(inode);
        return -ENOTDIR;
    }

    struct fs_dirent entries[DIRECTORY_ENTS_PER_BLK];
    block_read(entries, inode->ptrs[0], 1);
    struct stat *sb = malloc(sizeof(*sb));
    for (int j=0; j < DIRECTORY_ENTS_PER_BLK; j++) {
        if (entries[j].valid) {
            set_attr(inode, sb);
            filler(ptr, entries[j].name, sb, 0);
        }
    }

    free(inode);
    return 0;
}

int get_free_blk(void) {
    for (int i = 0; i < FS_BLOCK_SIZE; i++) {
        if (!bit_test(bitmap, i)) {
            return i;
        }
    }
    return -ENOSPC;
}

int get_free_dirent(struct fs_dirent *entries) {
    for (int j = 0; j < DIRECTORY_ENTS_PER_BLK; j++) {
        if (!entries[j].valid) {
            return j;
        }
    }
    return -ENOSPC;
}

int create_inode(mode_t mode, int inum) {
    struct fs_inode *inode = malloc(sizeof(*inode));
    struct fuse_context *ctx = fuse_get_context();
    uint16_t uid = ctx->uid;
    uint16_t gid = ctx->gid;
    inode->uid = uid;
    inode->gid = gid;
    inode->mode = mode;
    inode->mtime = inode->ctime = time(NULL);
    inode->size = FS_BLOCK_SIZE;
    memset(inode->ptrs, 0, (FS_BLOCK_SIZE/4 - 5) * sizeof(uint32_t));
    block_write(inode, inum, 1);

    free(inode);
    return 0;
}

int create_empty_entries(struct fs_dirent *entries) {
    for (int j = 0; j < DIRECTORY_ENTS_PER_BLK; j++) {
        entries[j].valid = 0;
    }
    return 0;
}
/* create - create a new file with specified permissions
 *
 * success - return 0
 * errors - path resolution, EEXIST
 *          in particular, for create("/a/b/c") to succeed,
 *          "/a/b" must exist, and "/a/b/c" must not.
 *
 * Note that 'mode' will already have the S_IFREG bit set, so you can
 * just use it directly. Ignore the third parameter.
 *
 * If a file or directory of this name already exists, return -EEXIST.
 * If there are already 128 entries in the directory (i.e. it's filled an
 * entire block), you are free to return -ENOSPC instead of expanding it.
 */
int fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    char *_path = strdup(path);
    char *pathv[MAX_NAME_LEN];
    int pathc = parse(_path, pathv);
    int inum = translate(pathc, pathv);
    int parent_inum = translate(pathc-1, pathv);
    char name[MAX_NAME_LEN];
    strcpy(name, pathv[pathc-1]);
    free(_path);

    if (inum > 0) return -EEXIST;
    if (parent_inum < 0 ) return parent_inum;
    
    struct fs_inode *parent_inode = malloc(sizeof(*parent_inode));
    block_read(parent_inode, parent_inum, 1);
    if (!S_ISDIR(parent_inode->mode)) return -ENOTDIR;
    
    // find a free entry on parent dir
    // find a free blk to store file inode
    struct fs_dirent parent_entries[DIRECTORY_ENTS_PER_BLK];
    block_read(parent_entries, parent_inode->ptrs[0], 1);
    int free_block = get_free_blk();
    int free_dirent = get_free_dirent(parent_entries);
    if (free_block < 0 || free_dirent < 0) {
        free(parent_inode);
        return -ENOSPC;
    }
    bit_set(bitmap, free_block);
    block_write(bitmap, 1, 1);

    // create file inode
    if (create_inode(mode, free_block) != 0) {         
        free(parent_inode);
        return -ENOSPC;
    }
    // push new inode onto parent dir's entry
    parent_entries[free_dirent].valid = 1;
    strcpy(parent_entries[free_dirent].name, name);
    parent_entries[free_dirent].inode = free_block;
    block_write(parent_entries, parent_inode->ptrs[0], 1);

    // find another free blk to store file content
    inum = free_block;
    free_block = get_free_blk();
    if (free_block < 0) {
        free(parent_inode);
        return -ENOSPC;
    }
    bit_set(bitmap, free_block);
    block_write(bitmap, 1, 1);

    // file inode -> file content blk
    struct fs_inode *inode = malloc(sizeof(*inode));
    block_read(inode, inum, 1);
    char *buf[FS_BLOCK_SIZE];
    memset(buf, 0, FS_BLOCK_SIZE);
    inode->ptrs[0] = free_block;
    block_write(inode, inum, 1);
    block_write(buf, free_block, 1);

    free(parent_inode);
    free(inode);
    return 0;
}

/* mkdir - create a directory with the given mode.
 *
 * WARNING: unlike fs_create, @mode only has the permission bits. You
 * have to OR it with S_IFDIR before setting the inode 'mode' field.
 *
 * success - return 0
 * Errors - path resolution, EEXIST
 * Conditions for EEXIST are the same as for create. 
 */ 
int fs_mkdir(const char *path, mode_t mode)
{
    mode |= S_IFDIR;
    char *_path = strdup(path);
    char *pathv[MAX_NAME_LEN];
    int pathc = parse(_path, pathv);
    int inum = translate(pathc, pathv);
    int parent_inum = translate(pathc-1, pathv);
    char name[MAX_NAME_LEN];
    strcpy(name, pathv[pathc-1]);
    free(_path);

    if (inum > 0) return -EEXIST;
    if (parent_inum < 0 ) return parent_inum;
    
    struct fs_inode *parent_inode = malloc(sizeof(*parent_inode));
    block_read(parent_inode, parent_inum, 1);
    if (!S_ISDIR(parent_inode->mode)) {
        free(parent_inode);
        return -ENOTDIR;
    }

    // find a free entry on parent dir
    // find a free blk to store dir inode
    struct fs_dirent parent_entries[DIRECTORY_ENTS_PER_BLK];
    block_read(parent_entries, parent_inode->ptrs[0], 1);
    int free_block = get_free_blk();
    int free_dirent = get_free_dirent(parent_entries);
    if (free_block < 0 || free_dirent < 0) {
        free(parent_inode);
        return -ENOSPC;
    }
    bit_set(bitmap, free_block);
    block_write(bitmap, 1, 1);

    // create dir inode
    if (create_inode(mode, free_block) != 0) {         
        free(parent_inode);
        return -ENOSPC;
    }

    // push new inode onto parent dir's entry
    parent_entries[free_dirent].valid = 1;
    strcpy(parent_entries[free_dirent].name, name);
    parent_entries[free_dirent].inode = free_block;
    block_write(parent_entries, parent_inode->ptrs[0], 1);

    // find another free block to store empty dir entries
    int dirent_free_block = get_free_blk();
    if (dirent_free_block < 0) {
        free(parent_inode);
        return -ENOSPC;
    }
    bit_set(bitmap, dirent_free_block);
    block_write(bitmap, 1, 1);

    struct fs_dirent entries[DIRECTORY_ENTS_PER_BLK];
    create_empty_entries(entries);
    block_write(entries, dirent_free_block, 1);

    // dir inode -> empty dir entries
    struct fs_inode *inode = malloc(sizeof(*parent_inode));
    block_read(inode, free_block, 1);
    inode->ptrs[0] = dirent_free_block;
    block_write(inode, free_block, 1);

    free(inode);
    return 0;
}

int clear_blks(struct fs_inode *inode) {
    int xblks = (inode->size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    for (int i = 0; i < xblks; i++) {
        int dblk = inode->ptrs[i];
        bit_clear(bitmap, dblk);  
    }
    return 0;
}
int clear_inode(int inum) {
    struct fs_inode *inode = malloc(sizeof(*inode));
    block_read(inode, inum, 1);
    memset(inode, 0, sizeof(struct fs_inode));
    bit_clear(bitmap, inum);
    block_write(bitmap, 1, 1);
    free(inode);
    return 0;
}
/* unlink - delete a file
 *  success - return 0
 *  errors - path resolution, ENOENT, EISDIR
 */
int fs_unlink(const char *path)
{
    char *_path = strdup(path);
    char *pathv[MAX_NAME_LEN];
    int pathc = parse(_path, pathv);
    int inum = translate(pathc, pathv);
    int parent_inum = translate(pathc-1, pathv);
    char name[MAX_NAME_LEN];
    strcpy(name, pathv[pathc-1]);
    free(_path);
    if (inum < 0) return inum;

    struct fs_inode *inode = malloc(sizeof(*inode));
    block_read(inode, inum, 1);
    if (S_ISDIR(inode->mode)) return -EISDIR;

    // remove entry from parent dir
    struct fs_inode *parent_inode = malloc(sizeof(*inode));
    block_read(parent_inode, parent_inum, 1);
    struct fs_dirent entries[DIRECTORY_ENTS_PER_BLK];
    block_read(entries, parent_inode->ptrs[0], 1);
    for (int i = 0; i < DIRECTORY_ENTS_PER_BLK; i++) {
        if (entries[i].valid && strcmp(entries[i].name, name) == 0) {
            memset(&entries[i], 0, sizeof(struct fs_dirent));
        }
    }
    block_write(entries, parent_inode->ptrs[0], 1);
    
    clear_blks(inode);
    clear_inode(inum);

    free(inode);
    free(parent_inode);
    return 0;
}
/* 1 is empty, 0 is not empty
 */
int is_empty_dir(struct fs_dirent *entries) {
    int empty = 1;
    for (int j = 0; j < DIRECTORY_ENTS_PER_BLK; j++) {
        if (entries[j].valid) {
            return 0;
        }
    }
    return empty;
}
/* rmdir - remove a directory
 *  success - return 0
 *  Errors - path resolution, ENOENT, ENOTDIR, ENOTEMPTY
 */
int fs_rmdir(const char *path)
{

    if (strcmp(path, "/") == 0) return -ENOTDIR;

    char *_path = strdup(path);
    char *pathv[MAX_NAME_LEN];
    int pathc = parse(_path, pathv);
    int inum = translate(pathc, pathv);
    int parent_inum = translate(pathc-1, pathv);
    char name[MAX_NAME_LEN];
    strcpy(name, pathv[pathc-1]);
    free(_path);
    
    if (inum < 0) return inum;
    if (parent_inum < 0) return parent_inum;
    struct fs_inode *inode = malloc(sizeof(*inode));
    struct fs_inode *parent_inode = malloc(sizeof(*parent_inode));
    block_read(inode, inum, 1);
    block_read(parent_inode, parent_inum, 1);
    if (!S_ISDIR(inode->mode) || !S_ISDIR(parent_inode->mode)) {
        free(inode);
        free(parent_inode);
        return -ENOTDIR;
    }

   // check if entries under cur dir is empty
    struct fs_dirent entries[DIRECTORY_ENTS_PER_BLK];
    block_read(entries, inode->ptrs[0], 1);
    if (!is_empty_dir(entries)) {
        free(inode);
        free(parent_inode);
        return -ENOTEMPTY;
    }

    // remove entry from parent dir
    struct fs_dirent parent_entries[DIRECTORY_ENTS_PER_BLK];
    block_read(parent_entries, parent_inode->ptrs[0], 1);
    for (int i = 0; i < DIRECTORY_ENTS_PER_BLK; i++) {
        if (parent_entries[i].valid && strcmp(parent_entries[i].name, name) == 0) {
            memset(&parent_entries[i], 0, sizeof(struct fs_dirent));
        }
    }
    block_write(parent_entries, parent_inode->ptrs[0], 1);

    // clear blks and inode
    clear_blks(inode);
    clear_inode(inum);
    
    free(inode);
    free(parent_inode);
    
    return 0;
}

/* rename - rename a file or directory
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
int fs_rename(const char *src_path, const char *dst_path)
{
    // parse path
    char *_src_path = strdup(src_path);
    char *src_pathv[MAX_NAME_LEN];
    int src_pathc = parse(_src_path, src_pathv);
    char *_dst_path = strdup(dst_path);
    char *dst_pathv[MAX_NAME_LEN];
    int dst_pathc = parse(_dst_path, dst_pathv);
    // translate path
    int src_inum = translate(src_pathc, src_pathv);
    int dst_inum = translate(dst_pathc, dst_pathv);
    int parent_src_inum = translate(src_pathc-1, src_pathv);
    int parent_dst_inum = translate(dst_pathc-1, dst_pathv);
    // if src does not exist
    if (src_inum < 0) return src_inum;
    if (parent_src_inum < 0) return parent_src_inum;
    // if dst already exist 
    if (dst_inum >= 0) return -EEXIST;
    // src and dst are not in the same directory
    if (parent_src_inum != parent_dst_inum) return -EINVAL;
    //get parent directory inode
    char src_name[MAX_NAME_LEN];
    char dst_name[MAX_NAME_LEN];
    strcpy(src_name, src_pathv[src_pathc-1]);
    strcpy(dst_name, dst_pathv[dst_pathc-1]);
    free(_src_path);
    free(_dst_path);
    //read parent src inode
    struct fs_inode *inode = malloc(sizeof(*inode));
    block_read(inode, parent_src_inum, 1);
    if (!S_ISDIR(inode->mode)) return -ENOTDIR;

    struct fs_dirent entries[DIRECTORY_ENTS_PER_BLK];
    block_read(entries, inode->ptrs[0], 1);

    // change src name to dst name 
    for (int i = 0; i < DIRECTORY_ENTS_PER_BLK; i++) {
        if (entries[i].valid && strcmp(entries[i].name, src_name) == 0) {
            memset(entries[i].name, 0, sizeof(entries[i].name));
            strcpy(entries[i].name, dst_name);
        }
    }
    block_write(entries, inode->ptrs[0], 1);

    return 0;
}

/* chmod - change file permissions
 *
 * success - return 0
 * Errors - path resolution, ENOENT.
 */
int fs_chmod(const char *path, mode_t mode)
{
    char *_path = strdup(path);
    char *pathv[MAX_NAME_LEN];
    int pathc = parse(_path, pathv);
    int inum = translate(pathc, pathv);
    free(_path);
    if (inum < 0) return inum;

    struct fs_inode *inode = malloc(sizeof(*inode));
    block_read(inode, inum, 1);
    inode->mode = mode;
    block_write(inode, inum, 1);
    free(inode);
    return 0;
}

/* utime - change access and modification times
 *         (for definition of 'struct utimebuf', see 'man utime')
 *
 * success - return 0
 * Errors - path resolution, ENOENT.
 */
int fs_utime(const char *path, struct utimbuf *ut)
{
    char *_path = strdup(path);
    char *pathv[MAX_NAME_LEN];
    int pathc = parse(_path, pathv);
    int inum = translate(pathc, pathv);
    free(_path);

    if (inum < 0) return inum;

    struct fs_inode *inode = malloc(sizeof(*inode));
    block_read(inode, inum, 1);
    inode->mtime = ut->modtime;
    block_write(inode, inum, 1);
    free(inode);

    return 0;
}

/* truncate - truncate file to exactly 'len' bytes
 * success - return 0
 * Errors - path resolution, ENOENT, EISDIR, EINVAL
 *    return EINVAL if len > 0.
 */
int fs_truncate(const char *path, off_t len)
{
    /* you can cheat by only implementing this for the case of len==0,
     * and an error otherwise.
     */
    if (len != 0) return -EINVAL;  /* invalid argument */

    char *_path = strdup(path);
    char *pathv[MAX_NAME_LEN];
    int pathc = parse(_path, pathv);
    int inum = translate(pathc, pathv);
    free(_path);
    if (inum < 0) return inum;

    struct fs_inode *inode = malloc(sizeof(*inode));
    block_read(inode, inum, 1);
    if (S_ISDIR(inode->mode)) {
        free(inode);
        return -EISDIR;
    }

    // truncate clear 
    int xblks = (inode->size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    char *buf = malloc(FS_BLOCK_SIZE);
    for (int i = 0; i < xblks; i++) {
        block_read(buf, inode->ptrs[i], 1);
        memset(buf, 0, FS_BLOCK_SIZE);
        block_write(buf, inode->ptrs[i], 1);
        if (i != 0) {
            bit_clear(bitmap, inode->ptrs[i]);
            block_write(bitmap, 1, 1);
        }
    }
    free(buf);
    free(inode);


    return 0;
}


/* read - read data from an open file.
 * success: should return exactly the number of bytes requested, except:
 *   - if offset >= file len, return 0
 *   - if offset+len > file len, return #bytes from offset to end
 *   - on error, return <0
 * Errors - path resolution, ENOENT, EISDIR
 */
#define MIN(a, b) ((a) < (b) ? (a) : (b))

int fs_read(const char *path, char *buf, size_t len, off_t offset,
	    struct fuse_file_info *fi)
{
    char *_path = strdup(path);
    char *pathv[MAX_NAME_LEN];
    int pathc = parse(_path, pathv);
    int inum = translate(pathc, pathv);
    free(_path);
    if (inum < 0) return inum;

    struct fs_inode *inode = malloc(sizeof(*inode));
    block_read(inode, inum, 1);
    if (S_ISDIR(inode->mode)) return -EISDIR;
    if (offset >= inode->size) return -EINVAL;


    int len_to_read = len;
    int file_size = inode->size;
    // if required read more than file size, read till EOF
    if (offset + len > inode->size) {
        len_to_read = inode->size - offset;
    }
    // find start blk number and offset
    int idx = offset / FS_BLOCK_SIZE;
    int blk_offset = offset % FS_BLOCK_SIZE;

    // read one blk at a time
    char temp[FS_BLOCK_SIZE];
    memset(temp, 0, FS_BLOCK_SIZE);
    int cur_read = 0;
    int total_read = 0;
    while (len_to_read > 0) {
        block_read(temp, inode->ptrs[idx], 1);
        cur_read = MIN(len_to_read, FS_BLOCK_SIZE - blk_offset);
        memcpy(buf + total_read, temp + blk_offset, cur_read);
        total_read += cur_read;
        len_to_read -= cur_read;
        blk_offset = 0;
        idx += 1;
    }
    
    return total_read;
}

/* write - write data to a file
 * success - return number of bytes written. (this will be the same as
 *           the number requested, or else it's an error)
 * Errors - path resolution, ENOENT, EISDIR
 *  return EINVAL if 'offset' is greater than current file length.
 *  (POSIX semantics support the creation of files with "holes" in them, 
 *   but we don't)
 */
int fs_write(const char *path, const char *buf, size_t len,
	     off_t offset, struct fuse_file_info *fi)
{
    char *_path = strdup(path);
    char *pathv[MAX_NAME_LEN];
    int pathc = parse(_path, pathv);
    int inum = translate(pathc, pathv);
    free(_path);
    if (inum < 0) return inum;

    struct fs_inode *inode = malloc(sizeof(*inode));
    block_read(inode, inum, 1);
    if (S_ISDIR(inode->mode)) return -EISDIR;
    if (offset > inode->size) return -EINVAL;

    int len_to_write = len;
    int file_size = inode->size;
    int xblks = (inode->size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    // find start blk number and offset
    int idx = offset / FS_BLOCK_SIZE;
    int blk_offset = offset % FS_BLOCK_SIZE;
    // write one blk at a time
    char temp[FS_BLOCK_SIZE];
    memset(temp, 0, FS_BLOCK_SIZE);
    int cur_write = 0;
    int total_write = 0;
    // write within allocated blks
    while (len_to_write > 0 && idx < xblks) {
        block_read(temp, inode->ptrs[idx], 1);
        cur_write = MIN(len_to_write, FS_BLOCK_SIZE - blk_offset);
        memcpy(temp + blk_offset, buf + total_write, cur_write);
        block_write(temp, inode->ptrs[idx], 1);
        total_write += cur_write;
        len_to_write -= cur_write;
        blk_offset = 0;
        idx += 1;
    }
    // allocate more blks to write
    while (len_to_write > 0 && get_free_blk() > 0) {
        int free_block = get_free_blk();
        bit_set(bitmap, free_block);
        block_write(bitmap, 1, 1);
        inode->ptrs[idx] = free_block;

        cur_write = MIN(len_to_write, FS_BLOCK_SIZE);
        memcpy(temp + blk_offset, buf + total_write, cur_write);
        block_write(temp, free_block, 1);
        total_write += cur_write;
        len_to_write -= cur_write;
        
    }
    // update file size
    if (offset + total_write > inode->size) 
        inode->size = offset + total_write;
        block_write(inode, inum, 1);

    return total_write;
}

int count_free_blks(void) {
    int cnt = 0;
    for (int i=0; i < super.disk_size; i++) {
        if (!bit_test(bitmap, i)) {
            cnt++;
        }
    }
    return cnt;
}
/* statfs - get file system statistics
 * see 'man 2 statfs' for description of 'struct statvfs'.
 * Errors - none. Needs to work.
 */
int fs_statfs(const char *path, struct statvfs *st)
{
    /* needs to return the following fields (set others to zero):
     *   f_bsize = BLOCK_SIZE
     *   f_blocks = total image - (superblock + block map)
     *   f_bfree = f_blocks - blocks used
     *   f_bavail = f_bfree
     *   f_namemax = <whatever your max namelength is>
     *
     * it's OK to calculate this dynamically on the rare occasions
     * when this function is called.
     */
    /* your code here */
    memset(st, 0, sizeof(*st));
    st->f_bsize = FS_BLOCK_SIZE;
    st->f_blocks = (fsblkcnt_t) super.disk_size - 2;
    int free_blks_num = count_free_blks();
    st->f_bfree = (fsblkcnt_t) free_blks_num;
    st->f_bavail = st->f_bfree;
    st->f_namemax = MAX_NAME_LEN;
    return 0;
}

/* operations vector. Please don't rename it, or else you'll break things
 */
struct fuse_operations fs_ops = {
    .init = fs_init,            /* read-mostly operations */
    .getattr = fs_getattr,
    .readdir = fs_readdir,
    .rename = fs_rename,
    .chmod = fs_chmod,
    .read = fs_read,
    .statfs = fs_statfs,

    .create = fs_create,        /* write operations */
    .mkdir = fs_mkdir,
    .unlink = fs_unlink,
    .rmdir = fs_rmdir,
    .utime = fs_utime,
    .truncate = fs_truncate,
    .write = fs_write,
};

