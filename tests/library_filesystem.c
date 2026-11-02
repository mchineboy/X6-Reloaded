/* POSIX test adapters. ROM input is read-only; only the disposable cache may
 * be written, in a separately supplied temporary output directory. */
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "x6_directory.h"
#include "x6_runtime.h"
const char *x6_test_root, *x6_test_output;
uint32_t x6_test_opens, x6_test_closes;
uint32_t x6_test_file_opens, x6_test_file_closes, x6_test_fail_write;
uint32_t x6_test_fail_cache_open;
struct directory {DIR *dir;char path[2048];uint8_t entry[272];};
static int native(char *out, const char *path)
{
    if (!path || strlen(path) < 3) return 0;
    char relative[512];snprintf(relative, sizeof relative, "%s", path + 3);
    for (char *p = relative; *p; ++p) if (*p == '\\') *p = '/';
    int n = snprintf(out, 2048, "%s/%s", !strncmp(relative, "x6/", 3) ? x6_test_output : x6_test_root, relative);
    return n > 0 && n < 2048;
}
void *x6_directory_open_raw(const char *path)
{
    struct directory *d = calloc(1, sizeof *d);assert(d);
    if (!native(d->path, path) || !(d->dir = opendir(d->path))) {free(d);return 0;}
    ++x6_test_opens;return d;
}
const uint8_t *x6_directory_read_raw(void *handle)
{
    struct directory *d = handle;struct dirent *e;
    while ((e = readdir(d->dir))) {
        if (strlen(e->d_name) > 255) continue;
        char p[4096];struct stat st;snprintf(p, sizeof p, "%s/%s", d->path, e->d_name);
        if (lstat(p, &st) || S_ISLNK(st.st_mode)) continue;
        memset(d->entry, 0, sizeof d->entry);d->entry[4] = S_ISDIR(st.st_mode) ? 0x10 : 0;
        for (uint32_t i = 0; i < 8; ++i) d->entry[8 + i] = (uint8_t)((uint64_t)st.st_size >> (8 * i));
        strcpy((char *)d->entry + 16, e->d_name);return d->entry;
    }
    return 0;
}
int32_t x6_directory_close_raw(void *handle)
{
    struct directory *d = handle;int r = closedir(d->dir);free(d);++x6_test_closes;return r;
}
void *x6_fs_open_raw(const char *path, const char *mode)
{
    char p[2048];
    assert(path[0] == 'F' && !strchr(path, '/')); /* The former permissive adapter hid this boundary mismatch. */
    assert(!strcmp(mode, "rb") || (!strcmp(mode, "wb") && !strcmp(path, "F:\\x6\\LIBRARY.IDX")));
    if (x6_test_fail_cache_open && !strcmp(mode, "wb")) return 0;
    FILE *f = native(p, path) ? fopen(p, mode) : 0;
    if (f) ++x6_test_file_opens;
    return f;
}
uint32_t x6_fs_read(void *out, uint32_t size, uint32_t n, void *f) {return (uint32_t)fread(out, size, n, f);}
uint32_t x6_fs_write(const void *in, uint32_t size, uint32_t n, void *f) {return x6_test_fail_write ? 0 : (uint32_t)fwrite(in, size, n, f);}
int32_t x6_fs_seek(void *f, int32_t offset, int32_t origin) {return fseek(f, offset, origin);}
int32_t x6_fs_close(void *f) {++x6_test_file_closes;return fclose(f);}
