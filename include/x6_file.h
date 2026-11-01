#ifndef X6_FILE_H
#define X6_FILE_H
#include <stdint.h>
typedef struct x6_file x6_file;
enum x6_file_error { X6_FILE_OK, X6_FILE_ARGUMENT, X6_FILE_READ_ONLY,
    X6_FILE_OPEN_FAILED, X6_FILE_NO_MEMORY, X6_FILE_SEEK_FAILED,
    X6_FILE_TELL_FAILED, X6_FILE_CLOSE_FAILED };
/* Initial read-only stdio adapter; paths must be absolute on the SD F: drive.
 * Raw Melis fread is always invoked with element size 1. A short raw read is
 * treated as EOF: device-level I/O error classification remains unresolved. */
x6_file *x6_file_open(const char *path, const char *mode);
enum x6_file_error x6_file_open_error(void);
int32_t x6_file_close(x6_file *file);
uint32_t x6_file_read(void *data, uint32_t size, uint32_t count, x6_file *file);
int32_t x6_file_seek(x6_file *file, int32_t offset, int32_t origin);
int32_t x6_file_tell(x6_file *file);
int32_t x6_file_getc(x6_file *file);
int32_t x6_file_ungetc(int32_t value, x6_file *file);
char *x6_file_gets(char *buffer, int32_t capacity, x6_file *file);
uint32_t x6_file_eof(const x6_file *file);
enum x6_file_error x6_file_error(const x6_file *file);
void x6_file_clear_error(x6_file *file);
int32_t x6_file_size(const char *path);
#endif
