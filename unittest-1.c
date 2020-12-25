/*
 * file:        testing.c
 * description: libcheck test skeleton for file system project
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

START_TEST(getattr)
{    
    struct attrs {
        const char *path;
        int uid;
        int gid;
        int mode;
        int size;
        int ctime; 
        int mtime;
    } table_1[] = {
        {"/dir3/file.12k-", 0, 500, 0100777, 12287, 1565283152, 1565283167},
        {"/file.12k+", 0, 500, 0100666, 12289, 1565283152, 1565283167},
        {"/file.8k+", 500, 500, 0100666, 8195, 1565283152, 1565283167},
        {NULL}
    };
    struct stat *sb = malloc(sizeof(*sb));
    for (int i = 0; table_1[i].path != NULL; i++) {
        struct attrs attr = table_1[i];
        int rv = fs_ops.getattr(attr.path, sb);
        ck_assert(rv >= 0);
        ck_assert(sb->st_uid == attr.uid);
        ck_assert(sb->st_gid == attr.gid);
        ck_assert(sb->st_mode == attr.mode);
        ck_assert(sb->st_size == attr.size);
        ck_assert(sb->st_ctime == attr.ctime);
        ck_assert(sb->st_mtime == attr.mtime);      
    }
    free(sb);
}
END_TEST

START_TEST(getattr_errors)
{  /*
    * ENOENT - "/not-a-file"
    * ENOTDIR - "/file.1k/file.0"
    * ENOENT on a middle part of the path - "/not-a-dir/file.0"
    * ENOENT in a subdirectory "/dir2/not-a-file"
    */
    const char *invalid_path[] = {"/not-a-file", 
                        "/file.1k/file.0",   
                        "/not-a-dir/file.0", 
                        "/dir2/not-a-file",
                        NULL
                        };
    int errors[] = {ENOENT, ENOTDIR, ENOENT, ENOENT};

    struct stat *sb = malloc(sizeof(*sb));
    for (int i = 0; invalid_path[i] != NULL; i++) {
        int rv = fs_ops.getattr(invalid_path[i], sb);        
        ck_assert(rv == -errors[i]);
    }
    free(sb);
}
END_TEST

struct dir_entry {
    char *name;
    int   seen;
    int   error;
};
int test_filler(void *ptr, const char *name, const struct stat *st, off_t off)
{        
    struct dir_entry *s;
    int found = -2;
    for (s = ptr ; s->name != NULL; s++) {
        if (strcmp(s->name, name) == 0) {
            if (s->seen == 1) // already marked seen
            {
               s->error = -1;
               return 0;
            }
            s->seen = 1;
            found = 0;
        };        
    }
    s->error = found;
    return 0;
}

int reset_dir_table(struct dir_entry *dir_table) {
    struct dir_entry *s;
    for (s = dir_table ; s->name != NULL; s++) {
        s->seen = 0;
    }
    return 0;
}

int _readdir(const char *path, struct dir_entry *dir_table) {
    int rv = fs_ops.readdir(path, dir_table, test_filler, 0, NULL);
    ck_assert(rv >= 0);
    struct dir_entry *s;
    for (s = dir_table ; s->name != NULL; s++) {
        ck_assert(s->seen == 1);
        ck_assert(s->error == 0);
    }
    return 0;
}

START_TEST(readdir)
{  
    struct dir_entry dir3_table[] = {
        {"subdir", 0, 0},
        {"file.12k-", 0, 0},
        {NULL}
    };
    struct dir_entry subdir_table[] = {
        {"file.4k-", 0, 0},
        {"file.8k-", 0, 0},
        {"file.12k", 0, 0},
        {NULL}
    };
    const char *paths[] = {
        "/dir3", 
        "/dir3/subdir",
        NULL
    };
    
    _readdir(paths[0], dir3_table);
    reset_dir_table(dir3_table);
    _readdir(paths[1], subdir_table);

}
END_TEST

START_TEST(readdir_errors)
{  
    const char *invalid_path[] = {"/dir2/file.4k+", 
                                "/dir2/not-a-file",
                                NULL};
        
    int errors[] = {ENOTDIR, ENOENT};

    struct stat *sb = malloc(sizeof(*sb));
    for (int i = 0; invalid_path[i] != NULL; i++) {
        int rv = fs_ops.readdir(invalid_path[i], sb, test_filler, 0, NULL);
        ck_assert(rv == -errors[i]);
    }
    free(sb);
}
END_TEST


START_TEST(big_read)
{  
    
    struct {
        char *path;
        int  len;
        unsigned cksum;  
    } table_1[] = {
        {"/file.1k", 1000, 1786485602},
        {"/file.10", 10, 855202508},
        {"/dir-with-long-name/file.12k+", 12289, 4101348955},
        {"/dir2/twenty-seven-byte-file-name", 1000, 2575367502},
        {"/dir3/subdir/file.12k", 12288, 3243963207},
        {"/dir3/subdir/file.8k-", 8190, 4090922556},
        {"/dir3/subdir/file.4k-", 4095, 4220582896},
        {NULL}
    };

    for (int i = 0; table_1[i].path != NULL; i++) {
        int file_size = table_1[i].len;
        char *buf = malloc(sizeof(char) * file_size + 10);
        memset(buf, '0', sizeof(char) * file_size + 10);

        int rv = fs_ops.read(table_1[i].path, buf, file_size, 0, NULL);
        ck_assert(rv >= 0);

        unsigned cksum = crc32(0, buf, file_size);
        ck_assert(cksum == table_1[i].cksum);
        ck_assert(*(buf + file_size + 1) == '0'); // no too much data read
        free(buf);
    }


}
END_TEST

START_TEST(small_read)
{  
    int len[] = {17, 100, 1000, 1024, 1970, 3000};
    
    struct {
        char *path;
        int  len;
        unsigned cksum;  
    } table_1[] = {
        {"/file.1k", 1000, 1786485602},
        {"/file.10", 10, 855202508},
        {"/dir-with-long-name/file.12k+", 12289, 4101348955},
        {"/dir2/twenty-seven-byte-file-name", 1000, 2575367502},
        {"/dir3/subdir/file.12k", 12288, 3243963207},
        {"/dir3/subdir/file.8k-", 8190, 4090922556},
        {"/dir3/subdir/file.4k-", 4095, 4220582896},
        {NULL}
    };

    for (int i = 0; table_1[i].path != NULL; i++) {
        int file_size = table_1[i].len;
        unsigned cksum = 0;
        int n = sizeof(len)/sizeof(int);
        char *buf = malloc(sizeof(char) * file_size + 10);
        memset(buf, '0', sizeof(char) * file_size + 10);
        for (int j = 0; j < n; j++){
            int nchunks = (file_size + len[j] - 1) / len[j];
            int total_read = 0;
            int offset = 0;
            for (int k = 0; k < nchunks; k++) {
                int cur_read = len[j];
                if (k == nchunks - 1) {
                    if (file_size % len[j] != 0) {
                        cur_read = file_size % len[j]; // chunk is not full
                    }
                }
                int rv = fs_ops.read(table_1[i].path, buf + total_read, cur_read, offset, NULL);
                ck_assert(rv >= 0);

                offset += cur_read;
                total_read += cur_read;
            }
            cksum = crc32(0, buf, file_size);
            ck_assert(cksum == table_1[i].cksum);
            ck_assert(*(buf + file_size + 1) == '0'); // no too much data read
            memset(buf, '0', sizeof(char) * file_size + 10);
        }
        free(buf);
    }

}
END_TEST

START_TEST(statfs_test)
{    
    const char *paths[] = {
        "/",
        "/file.1k",
        "/dir2",
        "/dir-with-long-name/file.12k+",
        "/dir3/subdir/file.12k",
        NULL,
    };
    int fields[] = {4096, 398, 355, 355, 27};
    struct statvfs *st = malloc(sizeof(*st));
    for (int i = 0; paths[i] != NULL; i++) {
        int rv = fs_ops.statfs(paths[i], st);
        int j = 0;
        ck_assert(st->f_bsize == fields[j++]);
        ck_assert(st->f_blocks == fields[j++]);
        ck_assert(st->f_bfree == fields[j++]);
        ck_assert(st->f_bavail == fields[j++]);
        ck_assert(st->f_namemax == fields[j++]);
    }
    free(st);
}
END_TEST

START_TEST(change_mode)
{    
    const char *paths[] = {"/file.1k",
        "/file.10",
        "/dir2",
        "/dir3/subdir",
        NULL,
    };
    struct stat *sb = malloc(sizeof(*sb));
    // change file mode
    for (int i = 0; i < 2; i++) {
        int rv = fs_ops.getattr(paths[i], sb);
        ck_assert(rv >= 0);

        mode_t new_mode = S_IFREG | 0520;
        ck_assert(S_ISREG(new_mode)); 
        ck_assert(sb->st_mode != new_mode);
        
        rv = fs_ops.chmod(paths[i], new_mode);
        ck_assert(rv >= 0);

        rv = fs_ops.getattr(paths[i], sb);
        ck_assert(rv >= 0);
        ck_assert(sb->st_mode == new_mode);
    }
    // change dir mode
    for (int i = 2; paths[i] != NULL; i++) {
        int rv = fs_ops.getattr(paths[i], sb);
        ck_assert(rv >= 0);

        mode_t new_mode = S_IFDIR | 0200;
        ck_assert(S_ISDIR(new_mode)); 
        ck_assert(sb->st_mode != new_mode);
        
        rv = fs_ops.chmod(paths[i], new_mode);
        ck_assert(rv >= 0);

        rv = fs_ops.getattr(paths[i], sb);
        ck_assert(rv >= 0);
        ck_assert(sb->st_mode == new_mode);
    }
    free(sb);

}
END_TEST
START_TEST(rename_file)
{  
    const char *src[] = {"/file.1k",
        "/dir2/file.4k+",
        "/dir3/subdir/file.12k",
        NULL,
    };

    const char *dst[] = {"/new-file.1k",
        "/dir2/new-file.4k+",
        "/dir3/subdir/new-file.12k",
        NULL,
    };
    // src file cksum should equal dst file cksum
    struct stat *sb = malloc(sizeof(*sb));
    for (int i = 0; src[i] != NULL; i++) {  
        int rv = fs_ops.getattr(src[i], sb);
        int file_size = sb->st_size;
        char *buf1 = malloc(sizeof(char) * file_size + 10);

        rv = fs_ops.read(src[i], buf1, file_size, 0, NULL);
        ck_assert(rv >= 0);
        unsigned old_cksum = crc32(0, buf1, file_size);

        rv = fs_ops.rename(src[i], dst[i]);
        ck_assert(rv >= 0);

        rv = fs_ops.getattr(dst[i], sb);
        ck_assert(rv >= 0);
        file_size = sb->st_size;
        char *buf2 = malloc(sizeof(char) * file_size + 10);
        rv = fs_ops.read(dst[i], buf2, file_size, 0, NULL);
        ck_assert(rv >= 0);

        unsigned cksum = crc32(0, buf2, file_size);
        ck_assert(cksum == old_cksum);

        free(buf1);
        free(buf2);
    }
    free(sb);

}
END_TEST

int rename_filler(void *buf, const char *name, const struct stat *st, off_t off)
{        
    strcpy(buf, name);
    return 0;
}

START_TEST(rename_dir)
{  
    const char *src[] = {
        "/dir2",
        "/dir3/subdir",
        NULL,
    };

    const char *dst[] = {
        "/new-dir2",
        "/dir3/new-subdir",
        NULL,
    };

    //  file read from src dir should equal that from dst dir
    for (int i = 0; i < src[i] != NULL; i++) {  
        char *buf1 = malloc(sizeof(char) * 27 * 10);
        int rv = fs_ops.readdir(src[i], buf1, rename_filler, 0, NULL);
        ck_assert(rv >= 0);

        rv = fs_ops.rename(src[i], dst[i]);
        ck_assert(rv >= 0);

        char *buf2 = malloc(sizeof(char) * 27 * 10);
        rv = fs_ops.readdir(dst[i], buf2, rename_filler, 0, NULL);
        ck_assert(rv >= 0);
        ck_assert_str_eq(buf1, buf2);
    }
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
    system("python gen-disk.py -q disk1.in test1.img");

    block_init("test1.img");
    fs_ops.init(NULL);
    
    Suite *s = suite_create("fs5600");
    TCase *tc = tcase_create("read_mostly");

    tcase_add_test(tc, getattr); 
    tcase_add_test(tc, getattr_errors); 
    tcase_add_test(tc, readdir); 
    tcase_add_test(tc, readdir_errors); 
    tcase_add_test(tc, big_read); 
    tcase_add_test(tc, small_read); 
    tcase_add_test(tc, statfs_test); 
    tcase_add_test(tc, change_mode); 
    tcase_add_test(tc, rename_file); 
    tcase_add_test(tc, rename_dir);

    suite_add_tcase(s, tc);
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    
    srunner_run_all(sr, CK_VERBOSE);
    int n_failed = srunner_ntests_failed(sr);
    printf("%d tests failed\n", n_failed);
    
    srunner_free(sr);
    return (n_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
