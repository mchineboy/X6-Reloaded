#include "x6_file.h"
#include "x6_runtime.h"
struct x6_file {
    void *handle;
    enum x6_file_error error;
    uint32_t eof;
    int32_t pushed;
};
static enum x6_file_error open_error;

static uint32_t normalize_path(char *out, const char *path)
{
    if (!path || (path[0] != 'F' && path[0] != 'f') || path[1] != ':'
        || (path[2] != '/' && path[2] != '\\')) return 0;
    out[0] = 'F';out[1] = ':';
    uint32_t start = 3;
    for (uint32_t i = 2; i < 256; ++i) {
        unsigned char c = (unsigned char)path[i];
        if ((c && c < 32) || c == ':') return 0;
        out[i] = c == '/' ? '\\' : (char)c;
        if (out[i] == '\\' || !out[i]) {
            uint32_t length = i >= start ? i - start : 0;
            if (length == 2 && out[start] == '.' && out[start + 1] == '.') return 0;
            start = i + 1;
        }
        if (!c) return i > 3;
    }
    return 0;
}

x6_file *x6_file_open(const char *path, const char *mode)
{
    char normalized[256];open_error = X6_FILE_OK;
    if (!mode || !normalize_path(normalized, path)) {open_error = X6_FILE_ARGUMENT;return 0;}
    if (!(mode[0] == 'r' && (!mode[1] || (mode[1] == 'b' && !mode[2])))) {
        open_error = X6_FILE_READ_ONLY;return 0;
    }
    void *handle = x6_fs_open(normalized, "rb");
    if (!handle) {open_error = X6_FILE_OPEN_FAILED;return 0;}
    x6_file *file = x6_malloc((uint32_t)sizeof(*file));
    if (!file) {x6_fs_close(handle);open_error = X6_FILE_NO_MEMORY;return 0;}
    file->handle = handle;file->error = X6_FILE_OK;file->eof = 0;file->pushed = -1;
    return file;
}
enum x6_file_error x6_file_open_error(void) {return open_error;}

int32_t x6_file_close(x6_file *file)
{
    if (!file) return -1;
    int32_t result = x6_fs_close(file->handle);
    x6_free(file);
    return result == 0 ? 0 : -1;
}

uint32_t x6_file_read(void *data, uint32_t size, uint32_t count, x6_file *file)
{
    if (!file) return 0;
    uint64_t total = (uint64_t)size * count;
    if (!total) return 0;
    if (!data || total > UINT32_MAX) {file->error = X6_FILE_ARGUMENT;return 0;}
    uint32_t read = 0, wanted = (uint32_t)total;
    if (file->pushed >= 0) {((uint8_t *)data)[0] = (uint8_t)file->pushed;file->pushed = -1;read = 1;}
    if (read < wanted) {
        uint32_t actual = x6_fs_read((uint8_t *)data + read, 1, wanted - read, file->handle);
        if (actual > wanted - read) {file->error = X6_FILE_ARGUMENT;return 0;}
        read += actual;
    }
    if (read < wanted) file->eof = 1;
    return read / size;
}

int32_t x6_file_seek(x6_file *file, int32_t offset, int32_t origin)
{
    if (!file) return -1;
    if (origin < 0 || origin > 2 || (origin == 1 && file->pushed >= 0 && offset == INT32_MIN)) {
        file->error = X6_FILE_ARGUMENT;return -1;
    }
    if (origin == 1 && file->pushed >= 0) --offset;
    if (x6_fs_seek(file->handle, offset, origin) != 0) {file->error = X6_FILE_SEEK_FAILED;return -1;}
    file->pushed = -1;file->eof = 0;return 0;
}

int32_t x6_file_tell(x6_file *file)
{
    if (!file) return -1;
    int32_t position = x6_fs_tell(file->handle);
    if (position < 0 || (position == 0 && file->pushed >= 0)) {file->error = X6_FILE_TELL_FAILED;return -1;}
    return position - (file->pushed >= 0);
}
int32_t x6_file_getc(x6_file *file)
{
    uint8_t byte;return x6_file_read(&byte, 1, 1, file) == 1 ? byte : -1;
}
int32_t x6_file_ungetc(int32_t value, x6_file *file)
{
    if (!file || value < 0 || value > 255 || file->pushed >= 0 || x6_file_tell(file) <= 0) return -1;
    file->pushed = value;file->eof = 0;return value;
}
char *x6_file_gets(char *buffer, int32_t capacity, x6_file *file)
{
    if (!file) return 0;
    if (!buffer || capacity <= 0) {file->error = X6_FILE_ARGUMENT;return 0;}
    int32_t used = 0, byte;
    while (used < capacity - 1 && (byte = x6_file_getc(file)) != -1) {
        buffer[used++] = (char)byte;if (byte == '\n') break;
    }
    buffer[used] = 0;
    return used || capacity == 1 ? buffer : 0;
}
uint32_t x6_file_eof(const x6_file *file) {return file && file->eof;}
enum x6_file_error x6_file_error(const x6_file *file) {return file ? file->error : X6_FILE_ARGUMENT;}
void x6_file_clear_error(x6_file *file) {if (file) {file->error = X6_FILE_OK;file->eof = 0;}}
int32_t x6_file_size(const char *path)
{
    x6_file *file = x6_file_open(path, "rb");if (!file) return -1;
    int32_t size = x6_file_seek(file, 0, 2) == 0 ? x6_file_tell(file) : -1;
    if (x6_file_close(file) != 0) return -1;
    return size;
}
