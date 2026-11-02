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
uint32_t x6_test_directory_attempts, x6_test_rom_opens;
uint32_t x6_test_file_opens, x6_test_file_closes, x6_test_fail_write;
uint32_t x6_test_fail_cache_open;
uint32_t x6_test_fail_log_open, x6_test_fail_log_write, x6_test_fail_cache_close;
uint32_t x6_test_fail_title_read;
uint32_t x6_test_fail_log_close;
uint32_t x6_test_fail_cache_read, x6_test_fail_index_write, x6_test_fail_footer_write;
uint32_t x6_test_cache_writes;
static FILE *cache_writer, *log_writer;
static FILE *cache_reader;
static FILE *title_reader;
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
    ++x6_test_directory_attempts;
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
    uint32_t cache = !strcmp(path, "F:\\x6\\LIBRARY.IDX"), log = !strcmp(path, "F:\\x6\\SCAN.LOG");
    if (!strncmp(path, "F:\\game\\", 8) || !strncmp(path, "F:\\roms\\", 8)) ++x6_test_rom_opens;
    assert(!strcmp(mode, "rb") || (!strcmp(mode, "wb") && (cache || log)));
    if (!strcmp(mode, "wb") && ((cache && x6_test_fail_cache_open) || (log && x6_test_fail_log_open))) return 0;
    FILE *f = native(p, path) ? fopen(p, mode) : 0;
    if (f) ++x6_test_file_opens;
    if (f && !strcmp(mode, "wb")) {if (cache) cache_writer = f;if (log) log_writer = f;}
    if (f && cache && !strcmp(mode, "rb")) cache_reader = f;
    if (f && strstr(path, ".tsv")) title_reader = f;
    return f;
}
uint32_t x6_fs_read(void *out, uint32_t size, uint32_t n, void *f)
{
    return (f == title_reader && x6_test_fail_title_read) || (f == cache_reader && x6_test_fail_cache_read)
        ? 0xffffffffu : (uint32_t)fread(out, size, n, f);
}
uint32_t x6_fs_write(const void *in, uint32_t size, uint32_t n, void *f)
{
    if (f == cache_writer) ++x6_test_cache_writes;
    return (f == cache_writer && x6_test_fail_write) || (f == log_writer && x6_test_fail_log_write)
        || (f == cache_writer && x6_test_fail_index_write && n > 1024u)
        || (f == cache_writer && x6_test_fail_footer_write && n == 36u)
        ? 0 : (uint32_t)fwrite(in, size, n, f);
}
int32_t x6_fs_seek(void *f, int32_t offset, int32_t origin) {return fseek(f, offset, origin);}
int32_t x6_fs_tell(void *f) {return (int32_t)ftell(f);}
int32_t x6_fs_close(void *f)
{
    uint32_t cache = f == cache_writer, log = f == log_writer;
    if (cache) cache_writer = 0;if (f == log_writer) log_writer = 0;
    if (f == cache_reader) cache_reader = 0;
    if (f == title_reader) title_reader = 0;
    ++x6_test_file_closes;int32_t result = fclose(f);
    return cache && x6_test_fail_cache_close ? -7 : log && x6_test_fail_log_close ? 5 : result;
}
