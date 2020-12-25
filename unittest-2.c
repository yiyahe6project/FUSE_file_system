/*
 * file:        unittest-2.c
 * description: libcheck test skeleton, part 2
 */

#define _FILE_OFFSET_BITS 64
#define FUSE_USE_VERSION 26

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <check.h>
#include <zlib.h>
#include <fuse.h>
#include <stdlib.h>
#include <errno.h>


extern struct fuse_operations fs_ops;
extern void block_init(char *file);

#define FS_BLOCK_SIZE 4096

/* mockup for fuse_get_context. you can change ctx.uid, ctx.gid in 
 * tests if you want to test setting UIDs in mknod/mkdir
 */
struct fuse_context ctx = { .uid = 500, .gid = 500};
struct fuse_context *fuse_get_context(void)
{
    return &ctx;
}
struct dir_entry {
    char *name;
    int   seen;
};
int reset_dir_table(struct dir_entry *dir_table) {
    struct dir_entry *s;
    for (s = dir_table ; s->name != NULL; s++) {
        s->seen = 0;
    }
    return 0;
}

int test_filler(void *ptr, const char *name, const struct stat *st, off_t off)
{        
    struct dir_entry *s = ptr;
    if (strcmp(s->name, name) == 0) {
        s->seen = 1;
    };        
    return 0;
}

START_TEST(createfile_errors)
{  
    const char *invalid_path[] = {
                        "/dir3/fake-subdir/new-file",  // /a/b/c - b doesn't exist(ENOENT)
                        "/dir3/file.12k-/new-file",    // /a/b/c - b isn't directory (ENOTDIR)
                        "/dir3/subdir/file.12k",       // /a/b/c - c exists, is file (EEXIST)
                        "/dir3/subdir",                // /a/c - c exists, is directory (EEXIST)
                        NULL};               
    int errors[] = {ENOENT, ENOTDIR, EEXIST, EEXIST};
    mode_t mode = S_IFREG | 0777;
    ck_assert(S_ISREG(mode));
    for (int i = 0; invalid_path[i] != NULL; i++) {
        int rv = fs_ops.create(invalid_path[i], mode, NULL);
        ck_assert(rv == -errors[i]);
    }
}
END_TEST

START_TEST(unlink_errors)
{  
    const char *invalid_path[] = {
                        "/dir3/fake-subdir/new-file",   // /a/b/c - b doesn't exist(ENOENT)
                        "/dir3/file.12k-/new-file",     // /a/b/c - b isn't directory (ENOTDIR)
                        "/dir3/subdir/fake-file",       // /a/b/c - c doesn't exit (ENOENT)
                        "/dir3/subdir",                 // /a/c - c is directory (EISDIR)
                        NULL};               
    int errors[] = {ENOENT, ENOTDIR, ENOENT, EISDIR};
    for (int i = 0; invalid_path[i] != NULL; i++) {
        int rv = fs_ops.unlink(invalid_path[i]);
        ck_assert(rv == -errors[i]);
    }
}
END_TEST

START_TEST(mkdir_errors)
{  
    const char *invalid_path[] = {
                        "/dir3/fake-subdir/new-file",   // /a/b/c - b doesn't exist(ENOENT)
                        "/dir3/file.12k-/new-file",     // /a/b/c - b isn't directory (ENOTDIR)
                        "/dir3/subdir/file.12k",       // /a/b/c - c exists, is file (EEXIST)
                        "/dir3/subdir",               // /a/c - c exists, is directory (EEXIST)
                        NULL};               
    int errors[] = {ENOENT, ENOTDIR, EEXIST, EEXIST};
    mode_t mode = 0777;
    for (int i = 0; invalid_path[i] != NULL; i++) {
        int rv = fs_ops.mkdir(invalid_path[i], mode);
        ck_assert(rv == -errors[i]);
    }
}
END_TEST

START_TEST(rmdir_errors)
{  
    const char *invalid_path[] = {
                        "/dir3/fake-subdir/file.4k-",   // /a/b/c - b doesn't exist(ENOENT)
                        "/dir3/file.12k-/new-file",     // /a/b/c - b isn't directory (ENOTDIR)
                        "/dir3/subdir/fake-file",       // /a/b/c - c doesn't exit (ENOENT)
                        "/dir3/subdir/file.12k",        // /a/c - c is file (ENOTDIR)
                        "/dir3/subdir",                 // directory not empty (ENOTEMPTY)
                        NULL};               
    int errors[] = {ENOENT, ENOTDIR, ENOENT, ENOTDIR, ENOTEMPTY};
    for (int i = 0; invalid_path[i] != NULL; i++) {
        int rv = fs_ops.rmdir(invalid_path[i]);
        ck_assert(rv == -errors[i]);
    }
}
END_TEST

START_TEST(mkdir_rmdir)
{  
    struct dir_entry table[] = {
        {"new-dir1", 0},
        {"new-dir2", 0},
        {"new-dir3", 0},
        {NULL}
    };
    struct {
        const char *full_path;
        char  *dir_path;
    } paths[] = {
                {"/new-dir1", "/"},
                {"/dir2/new-dir2", "/dir2"}, 
                {"/dir3/subdir/new-dir3", "/dir3/subdir"},       
                {NULL}};               

    mode_t dir_mode = S_IFDIR | 0777;
    ck_assert(S_ISDIR(dir_mode));

    /* seen 0->1 after mkdir
     * reset seen to 0
     * seen 0->0 after rmdir
     */
    int rv = -1;
    for (int i = 0; paths[i].full_path!= NULL; i++) {
        rv = fs_ops.readdir(paths[i].dir_path, &table[i], test_filler, 0, NULL);
        ck_assert(rv >= 0);
        ck_assert(table[i].seen == 0);
        
        rv = fs_ops.mkdir(paths[i].full_path, dir_mode);
        ck_assert(rv >= 0);

        rv = fs_ops.readdir(paths[i].dir_path, &table[i], test_filler, 0, NULL);
        ck_assert(rv >= 0);
        ck_assert(table[i].seen == 1);

        rv = fs_ops.rmdir(paths[i].full_path); 
        ck_assert(rv >= 0);

        reset_dir_table(table);
        rv = fs_ops.readdir(paths[i].dir_path, &table[i], test_filler, 0, NULL);
        ck_assert(rv >= 0);
        ck_assert(table[i].seen == 0);
    }
}
END_TEST

START_TEST(create_unlink)
{  
    struct dir_entry table[] = {
        {"new-file1", 0},
        {"new-file2", 0},
        {"new-file3", 0},
        {NULL}
    };
    struct {
        const char *full_path;
        char  *dir_path;
    } paths[] = {
                {"/new-file1", "/"},
                {"/dir2/new-file2", "/dir2"}, 
                {"/dir3/subdir/new-file3", "/dir3/subdir"},       
                {NULL}};               

    mode_t f_mode = S_IFREG | 0777;
    ck_assert(S_ISREG(f_mode));

    /* seen 0->1 after create
     * reset seen to 0
     * seen 0->0 after unlink
     */
    int rv = -1;
    for (int i = 0; paths[i].full_path!= NULL; i++) {
        rv = fs_ops.readdir(paths[i].dir_path, &table[i], test_filler, 0, NULL);
        ck_assert(rv >= 0);
        ck_assert(table[i].seen == 0);
        
        rv = fs_ops.create(paths[i].full_path, f_mode, NULL);
        ck_assert(rv >= 0);

        rv = fs_ops.readdir(paths[i].dir_path, &table[i], test_filler, 0, NULL);
        ck_assert(rv >= 0);
        ck_assert(table[i].seen == 1);

        rv = fs_ops.unlink(paths[i].full_path); 
        ck_assert(rv >= 0);

        reset_dir_table(table);
        rv = fs_ops.readdir(paths[i].dir_path, &table[i], test_filler, 0, NULL);
        ck_assert(rv >= 0);
        ck_assert(table[i].seen == 0);
    }
}
END_TEST

START_TEST(write_errors)
{  
    struct {
        char *path;
        int  len;
        unsigned cksum;  
    } table_1[] = {
        {"/file.1k", 1000, 1786485602},     // offset > file size - EINVAL
        {"/dir3/subdir", 4095, 4220582896}, // a/b - b is not file - EISDIR
        {NULL}
    };
    int errors[] = {EINVAL, EISDIR};

    int size = 4000;
    char *buf = malloc(sizeof(char) * size + 10);
    memset(buf, 'W', sizeof(char) * size + 10);

    for (int i = 0; table_1[i].path != NULL; i++) {
        int rv = fs_ops.write(table_1[i].path, buf, size, 1500, NULL); 
        ck_assert(rv == -errors[i]);
    }

    free(buf);
}
END_TEST

START_TEST(write_data_test)
{     
    struct {
        char *path;
    } table_1[] = {
        {"/file.1k"}, 
        {"/dir3/subdir/file.4k-"}, 
        {"/file.10"}, 
        {"/dir-with-long-name/file.12k+"}, 
        {"/dir2/twenty-seven-byte-file-name"},
        {"/dir3/subdir/file.8k-"}, 
        {NULL}
    };

    int size = 4000;
    char *buf = malloc(sizeof(char) * size + 10);
    memset(buf, 'W', sizeof(char) * size + 10);
    char *read_buf = malloc(sizeof(char) * size + 10);
    memset(read_buf, 'R', sizeof(char) * size + 10);

    for (int i = 0; table_1[i].path != NULL; i++) {
        int rv = fs_ops.write(table_1[i].path, buf, size, 0, NULL);  
        ck_assert(rv == size);

        rv = fs_ops.read(table_1[i].path, read_buf, size, 0, NULL);
        ck_assert(rv == size);

        unsigned write_cksum = crc32(0, buf, size);
        unsigned read_cksum = crc32(0, read_buf, size);
        ck_assert(write_cksum == read_cksum);

        memset(read_buf, 'R', sizeof(char) * size + 10);
    }

    free(buf);
    free(read_buf);
}
END_TEST

START_TEST(append_test)
{  
    int len[] = {17, 100, 1000, 1024, 1970, 3000};
    
    struct {
        char *path;
        int  file_size;
    } table_1[] = {
        {"/dir3/subdir/new-file1", FS_BLOCK_SIZE - 1}, 
        {"/dir3/subdir/new-file2", FS_BLOCK_SIZE}, 
        {"/dir3/subdir/new-file3", FS_BLOCK_SIZE * 2 - 1}, 
        {"/dir3/subdir/new-file4", FS_BLOCK_SIZE * 2}, 
        {"/dir3/subdir/new-file5", FS_BLOCK_SIZE * 3 - 1},
        {"/dir3/subdir/new-file6", FS_BLOCK_SIZE * 3}, 
        {NULL}
    };

    int rv = -1;
    // get number of free blocks 
    struct statvfs *st = malloc(sizeof(*st));
    rv = fs_ops.statfs("/", st);
    int nfree_blks_before = st->f_bfree;
    // create empty files
    mode_t f_mode = S_IFREG | 0777;
    ck_assert(S_ISREG(f_mode));
    for (int i = 0; table_1[i].path != NULL; i++) {
        rv = fs_ops.create(table_1[i].path, f_mode, NULL);
        ck_assert(rv >= 0);
    }
    // write files with various sizes
    for (int i = 0; table_1[i].path != NULL; i++) {
        int size = table_1[i].file_size;
        char *buf = malloc(sizeof(char) * size + 10);
        memset(buf, 'W', sizeof(char) * size + 10);
        char *read_buf = malloc(sizeof(char) * size + 10);
        memset(read_buf, 'R', sizeof(char) * size + 10);
        int n = sizeof(len)/sizeof(int);
        for (int j = 0; j < n; j++){
            int nchunks = (size + len[j] - 1) / len[j];
            int total_write = 0;
            int offset = 0;
            for (int k = 0; k < nchunks; k++) {
                int cur_write = len[j];
                if (k == nchunks - 1) {
                    if (size % len[j] != 0) {
                        cur_write = size % len[j]; // chunk is not full
                    }
                }
                int rv = fs_ops.write(table_1[i].path, buf + total_write, cur_write, offset, NULL);  
                ck_assert(rv == cur_write);
            
                offset += cur_write;
                total_write += cur_write;
            }
            int rv = fs_ops.read(table_1[i].path, read_buf, size, 0, NULL);
            ck_assert(rv == size);
            unsigned write_cksum = crc32(0, buf, size);
            unsigned read_cksum = crc32(0, read_buf, size);
            ck_assert(write_cksum == read_cksum);
            memset(read_buf, 'R', sizeof(char) * size + 10);
        }
        free(buf);
        free(read_buf);
    }
    // the number of free blocks changes after create and write files
    rv = fs_ops.statfs("/", st);
    ck_assert(st->f_bfree != nfree_blks_before);
    // delete files
    for (int i = 0; table_1[i].path != NULL; i++) {
        rv = fs_ops.unlink(table_1[i].path);
        ck_assert(rv >= 0);
    }
    // the number of free blocks should equal to the beginning after delete
    rv = fs_ops.statfs("/", st);
    int nfree_blks_after = st->f_bfree;
    ck_assert(nfree_blks_after == nfree_blks_before);

    free(st);

}
END_TEST

START_TEST(overwrite_test)
{  
    struct {
        char *path; 
    } table_1[] = {
        {"/file.1k"}, 
        {"/dir3/subdir/file.4k-"}, 
        {"/file.10"}, 
        {"/dir-with-long-name/file.12k+"}, 
        {"/dir2/twenty-seven-byte-file-name"},
        {"/dir3/subdir/file.8k-"}, 
        {NULL}
    };

    int size = 4000;
    char *buf1 = malloc(sizeof(char) * size + 10);
    memset(buf1, '1', sizeof(char) * size + 10);
    char *buf2 = malloc(sizeof(char) * size + 10);
    memset(buf2, '2', sizeof(char) * size + 10);
    char *read_buf = malloc(sizeof(char) * size + 10);
    memset(read_buf, 'R', sizeof(char) * size + 10);

    for (int i = 0; table_1[i].path != NULL; i++) {
        int rv = fs_ops.write(table_1[i].path, buf1, size, 0, NULL);  
        ck_assert(rv == size);

        rv = fs_ops.write(table_1[i].path, buf2, size, 0, NULL);  
        ck_assert(rv == size);

        rv = fs_ops.read(table_1[i].path, read_buf, size, 0, NULL);
        ck_assert(rv == size);

        unsigned write_cksum = crc32(0, buf2, size);
        unsigned read_cksum = crc32(0, read_buf, size);
        ck_assert(write_cksum == read_cksum);
        memset(read_buf, 'R', sizeof(char) * size + 10);
    }

    free(buf1);
    free(buf2);
    free(read_buf);
}
END_TEST

START_TEST(truncate_test)
{  
    int len[] = {17, 100, 1000, 1024, 1970, 3000};
    
    struct {
        char *path;
        int  file_size;
    } table_1[] = {
        {"/dir3/subdir/new-file1", FS_BLOCK_SIZE - 1}, 
        {"/dir3/subdir/new-file2", FS_BLOCK_SIZE}, 
        {"/dir3/subdir/new-file3", FS_BLOCK_SIZE * 2 - 1}, 
        {"/dir3/subdir/new-file4", FS_BLOCK_SIZE * 2}, 
        {"/dir3/subdir/new-file5", FS_BLOCK_SIZE * 3 - 1},
        {"/dir3/subdir/new-file6", FS_BLOCK_SIZE * 3}, 
        {NULL}
    };
    int rv = -1;
    // create empty files
    mode_t f_mode = S_IFREG | 0777;
    ck_assert(S_ISREG(f_mode));
    for (int i = 0; table_1[i].path != NULL; i++) {
        rv = fs_ops.create(table_1[i].path, f_mode, NULL);
        ck_assert(rv >= 0);
    }
    // get number of free blocks after creating empty files
    struct statvfs *st = malloc(sizeof(*st));
    rv = fs_ops.statfs("/", st);
    int nfree_blks_before = st->f_bfree;
    // write files with various sizes
    for (int i = 0; table_1[i].path != NULL; i++) {
        int size = table_1[i].file_size;
        char *buf = malloc(sizeof(char) * size + 10);
        memset(buf, 'W', sizeof(char) * size + 10);
        char *read_buf = malloc(sizeof(char) * size + 10);
        memset(read_buf, 'R', sizeof(char) * size + 10);
        int n = sizeof(len)/sizeof(int);
        for (int j = 0; j < n; j++){
            int nchunks = (size + len[j] - 1) / len[j];
            int total_write = 0;
            int offset = 0;
            for (int k = 0; k < nchunks; k++) {
                int cur_write = len[j];
                if (k == nchunks - 1) {
                    if (size % len[j] != 0) {
                        cur_write = size % len[j]; // chunk is not full
                    }
                }
                int rv = fs_ops.write(table_1[i].path, buf + total_write, cur_write, offset, NULL);  
                ck_assert(rv == cur_write);
            
                offset += cur_write;
                total_write += cur_write;
            }
            int rv = fs_ops.read(table_1[i].path, read_buf, size, 0, NULL);
            ck_assert(rv == size);
            unsigned write_cksum = crc32(0, buf, size);
            unsigned read_cksum = crc32(0, read_buf, size);
            ck_assert(write_cksum == read_cksum);
            memset(read_buf, 'R', sizeof(char) * size + 10);
        }
        free(buf);
        free(read_buf);
    }
    // the number of free blocks changes after write files
    rv = fs_ops.statfs("/", st);
    ck_assert(st->f_bfree != nfree_blks_before);
    // truncate files
    for (int i = 0; table_1[i].path != NULL; i++) {
        rv = fs_ops.truncate(table_1[i].path, 0);
        ck_assert(rv >= 0);
    }
    // verify the number of free blocks
    rv = fs_ops.statfs("/", st);
    int nfree_blks_after = st->f_bfree;
    ck_assert(nfree_blks_before == nfree_blks_after);

    // return EINVAL error if offset is not 0
    rv = fs_ops.truncate(table_1[0].path, 1);
    ck_assert(rv == -EINVAL);

    free(st);

}
END_TEST



/* note that your tests will call:
 *  fs_ops.getattr(path, struct stat *sb)
 *  fs_ops.readdir(path, NULL, filler_function, 0, NULL)
 *  fs_ops.read(path, buf, len, offset, NULL);
 *  fs_ops.statfs(path, struct statvfs *sv);
 */


int main(int argc, char **argv)
{
    system("python gen-disk.py -q disk2.in test2.img");

    block_init("test2.img");
    fs_ops.init(NULL);
    
    Suite *s = suite_create("fs5600");
    TCase *tc = tcase_create("write_mostly");

    /* create/mkdir/unlink/rmdir tests*/
    tcase_add_test(tc, createfile_errors); 
    tcase_add_test(tc, mkdir_errors);
    tcase_add_test(tc, unlink_errors);
    tcase_add_test(tc, rmdir_errors);  
    tcase_add_test(tc, mkdir_rmdir);
    tcase_add_test(tc, create_unlink);

    /* write tests */
    tcase_add_test(tc, write_errors);
    tcase_add_test(tc, write_data_test); 
    tcase_add_test(tc, append_test); 
    tcase_add_test(tc, overwrite_test); 

    /* truncate test */
    tcase_add_test(tc, truncate_test); 

    suite_add_tcase(s, tc);
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    
    srunner_run_all(sr, CK_VERBOSE);
    int n_failed = srunner_ntests_failed(sr);
    printf("%d tests failed\n", n_failed);
    
    srunner_free(sr);
    return (n_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

