/* Bounded runtime test: SD marker is opened only in rb mode. No persistent
 * configuration, ROM parsing, giant allocations, or forced device OOM test. */
#include "x6_runtime.h"

static uint32_t equal(const char *a, const char *b, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static void check_file(struct x6_runtime_diagnostic *result)
{
    static const char expected[] = "X6-RELOADED-FILE-TEST-V1\n";
    char data[32];
    void *file = x6_fs_open("F:\\x6-probe-read.txt", "rb");
    if (!file)
        return;
    uint32_t count = x6_fs_read(data, 1, sizeof(data), file);
    result->read_ok = count == sizeof(expected) - 1 && equal(data, expected, sizeof(expected) - 1);
    if (result->read_ok) {
        /* Origins are inferred as SET=0, CUR=1, END=2 from stock callers. */
        uint32_t ok = x6_fs_tell(file) == 25;
        if (ok) ok = x6_fs_seek(file, 0, 2) == 0 && x6_fs_tell(file) == 25;
        if (ok) ok = x6_fs_read(data, 1, 1, file) == 0;
        if (ok) ok = x6_fs_seek(file, -1, 2) == 0 && x6_fs_tell(file) == 24;
        if (ok) ok = x6_fs_read(data, 1, 1, file) == 1 && data[0] == '\n';
        if (ok) ok = x6_fs_seek(file, 3, 0) == 0 && x6_fs_tell(file) == 3;
        if (ok) ok = x6_fs_read(data, 1, 7, file) == 7 && equal(data, expected + 3, 7);
        if (ok) ok = x6_fs_seek(file, -2, 1) == 0 && x6_fs_tell(file) == 8;
        if (ok) ok = x6_fs_read(data, 1, 3, file) == 3 && equal(data, expected + 8, 3);
        if (ok) ok = x6_fs_seek(file, 0, 0) == 0 && x6_fs_tell(file) == 0;
        if (ok) ok = x6_fs_read(data, 1, 1, file) == 1 && data[0] == 'X';
        result->seek_ok = ok;
    }
    if (x6_fs_close(file) != 0) {
        result->read_ok = 0;
        result->seek_ok = 0;
    }
}

static uint8_t pattern(uint32_t i)
{
    return (uint8_t)(i * 13u + 7u);
}

static void check_heap(struct x6_runtime_diagnostic *result)
{
    uint8_t *first = x6_malloc(512);
    uint8_t *second = x6_malloc(4096);
    uint8_t *zeroed = 0;
    if (!first || !second)
        goto cleanup;
    /* AAPCS double/int64 data needs eight-byte alignment. */
    if (((uintptr_t)first & 7u) || ((uintptr_t)second & 7u) || first == second)
        goto cleanup;
    for (uint32_t i = 0; i < 512; ++i) first[i] = pattern(i);
    for (uint32_t i = 0; i < 4096; ++i) second[i] = (uint8_t)(pattern(i) ^ 0x5au);
    uint32_t ok = 1;
    for (uint32_t i = 0; i < 512; ++i) if (first[i] != pattern(i)) ok = 0;
    for (uint32_t i = 0; i < 4096; ++i) if (second[i] != (uint8_t)(pattern(i) ^ 0x5au)) ok = 0;
    if (!ok)
        goto cleanup;
    result->heap_ok = 1;
    uint8_t *grown = x6_realloc(first, 8192);
    if (!grown)
        goto cleanup;
    first = grown;
    if ((uintptr_t)first & 7u)
        goto cleanup;
    for (uint32_t i = 0; i < 512; ++i) if (first[i] != pattern(i)) ok = 0;
    for (uint32_t i = 512; i < 8192; ++i) first[i] = pattern(i);
    for (uint32_t i = 0; i < 8192; ++i) if (first[i] != pattern(i)) ok = 0;
    if (!ok)
        goto cleanup;
    grown = x6_realloc(first, 128);
    if (!grown)
        goto cleanup;
    first = grown;
    if ((uintptr_t)first & 7u)
        goto cleanup;
    for (uint32_t i = 0; i < 128; ++i) if (first[i] != pattern(i)) ok = 0;
    for (uint32_t i = 0; i < 4096; ++i) if (second[i] != (uint8_t)(pattern(i) ^ 0x5au)) ok = 0;
    zeroed = x6_calloc(17, 19);
    if (!zeroed)
        goto cleanup;
    for (uint32_t i = 0; i < 323; ++i) if (zeroed[i] != 0) ok = 0;
    result->grow_ok = ok;
cleanup:
    x6_free(zeroed);
    if (second != first) x6_free(second);
    x6_free(first);
}

struct x6_runtime_diagnostic x6_check_runtime(void)
{
    struct x6_runtime_diagnostic result = {0, 0, 0, 0};
    check_file(&result);
    check_heap(&result);
    return result;
}
