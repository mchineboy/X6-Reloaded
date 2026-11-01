/* Read-only native audit of the production stock catalog against a local
 * backup. POSIX adapters simulate directory records, NOT Melis execution. */
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "x6_directory.h"
#include "x6_stock_catalog.h"
#include "x6_runtime.h"

static const char *backup;
struct directory {DIR *dir;char path[2048];uint8_t entry[16 + 256];};
static uint32_t opened, closed;
static int native_path(char *out, uint32_t capacity, const char *path)
{
    int n = snprintf(out, capacity, "%s/%s", backup, path + 3);
    if (n < 0 || (uint32_t)n >= capacity) return 0;
    for (int i = 0; i < n; ++i) if (out[i] == '\\') out[i] = '/';
    return 1;
}
void *x6_directory_open_raw(const char *path)
{
    struct directory *d = calloc(1, sizeof *d);
    if (!d) return 0;
    if (!native_path(d->path, sizeof d->path, path) || !(d->dir = opendir(d->path))) {free(d);return 0;}
    ++opened;return d;
}
const uint8_t *x6_directory_read_raw(void *handle)
{
    struct directory *d = handle;struct dirent *e;
    while ((e = readdir(d->dir))) {
        if (strlen(e->d_name) >= 256) continue;
        char path[4096];struct stat s;
        snprintf(path, sizeof path, "%s/%s", d->path, e->d_name);
        if (lstat(path, &s) || S_ISLNK(s.st_mode)) continue;
        memset(d->entry, 0, sizeof d->entry);
        d->entry[4] = S_ISDIR(s.st_mode) ? 0x10 : 0;
        uint64_t size = (uint64_t)s.st_size;
        for (uint32_t i = 0; i < 8; ++i) d->entry[8 + i] = (uint8_t)(size >> (i * 8));
        strcpy((char *)d->entry + 16, e->d_name);return d->entry;
    }
    return 0;
}
int32_t x6_directory_close_raw(void *handle)
{
    struct directory *d = handle;int result = closedir(d->dir);free(d);++closed;return result;
}
void *x6_fs_open(const char *path, const char *mode)
{
    char native[2048];assert(!strcmp(mode, "rb"));
    return native_path(native, sizeof native, path) ? fopen(native, mode) : 0;
}
uint32_t x6_fs_read(void *ptr, uint32_t size, uint32_t count, void *handle) {return (uint32_t)fread(ptr, size, count, handle);}
int32_t x6_fs_close(void *handle) {return fclose(handle);}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    backup = argv[1];
    struct x6_game *games = calloc(512, sizeof *games);if (!games) return 1;
    uint32_t n = x6_stock_catalog_build(games, 512);
    printf("Native backup audit: %u stock games; %u directory handles opened/closed; legacy index %u\n", n, opened, x6_stock_catalog_legacy(games, n));
    assert(opened == closed);
    for (uint32_t i = 0; i < n && i < 4; ++i) printf("  %s -> %s\n", games[i].title, games[i].path);
    free(games);
    puts("Local files only; this does not test the Melis syscalls or gameplay.");
    return n ? 0 : 1;
}
