/* ARM/newlib bridge. All file descriptors are SD read-only; fd 1/2 go to a
 * bounded in-memory diagnostic log. Unsupported POSIX calls report errors.
 * Never compile this into native host tests: it defines the libc syscall ABI. */
#include <errno.h>
#include <fcntl.h>
#include <reent.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <unistd.h>
#include "x6_runtime.h"
#include "x6_file.h"
#include "x6_guard.h"
#ifdef X6_FIVE_GAMES
#include "x6_large_memory.h"
#endif

#define FD_COUNT 32
static x6_file *files[FD_COUNT];
static char log_buffer[4096];
static uint32_t log_position;
struct call_guard {jmp_buf context;struct call_guard *previous;};
static struct call_guard *active_guard;

int32_t x6_guard_call(int32_t (*function)(void *), void *context)
{
    struct call_guard guard;guard.previous=active_guard;active_guard=&guard;
    int32_t result;
    if (setjmp(guard.context)) result=-127;
    else result=function(context);
    active_guard=guard.previous;return result;
}
_Noreturn void x6_runtime_abort(int status)
{
    (void)status;
    if (active_guard) longjmp(active_guard->context,1);
    __builtin_trap();
}
_Noreturn void _exit(int status) {x6_runtime_abort(status);}
_Noreturn void exit(int status) {x6_runtime_abort(status);}

#ifdef X6_FIVE_GAMES
void *_malloc_r(struct _reent *r,size_t n) {void *p=n<=UINT32_MAX?x6_libc_alloc((uint32_t)n):0;if(!p&&n)r->_errno=ENOMEM;return p;}
void *_calloc_r(struct _reent *r,size_t n,size_t size) {
 uint64_t bytes=(uint64_t)n*size;void *p=bytes<=UINT32_MAX?x6_libc_alloc((uint32_t)bytes):0;
 if(p)for(uint32_t i=0;i<(uint32_t)bytes;++i)((uint8_t *)p)[i]=0;
 else if(n&&size)r->_errno=ENOMEM;return p;
}
void *_realloc_r(struct _reent *r,void *old,size_t n) {void *p=n<=UINT32_MAX?x6_libc_resize(old,(uint32_t)n):0;if(!p&&n)r->_errno=ENOMEM;return p;}
void _free_r(struct _reent *r,void *p) {(void)r;x6_libc_free(p);}
#else
void *_malloc_r(struct _reent *reent,size_t size)
{
    void *p=size<2097152u?x6_malloc((uint32_t)size):0;
    if (!p && size) reent->_errno=ENOMEM;return p;
}
void *_calloc_r(struct _reent *reent,size_t count,size_t size)
{
    void *p=count<=UINT32_MAX && size<=UINT32_MAX && (uint64_t)count*size<2097152u?x6_calloc((uint32_t)count,(uint32_t)size):0;
    if (!p && count && size) reent->_errno=ENOMEM;return p;
}
void *_realloc_r(struct _reent *reent,void *ptr,size_t size)
{
    void *p=size<2097152u?x6_realloc(ptr,(uint32_t)size):0;
    if (!p && size) reent->_errno=ENOMEM;return p;
}
void _free_r(struct _reent *reent,void *ptr) {(void)reent;x6_free(ptr);}

#endif

int _open(const char *path,int flags,int mode)
{
    (void)mode;
    if ((flags & ~O_BINARY)!=O_RDONLY) {errno=EROFS;return -1;}
    int fd;
    for (fd=3;fd<FD_COUNT;++fd) if (!files[fd]) break;
    if (fd==FD_COUNT) {errno=EMFILE;return -1;}
    files[fd]=x6_file_open(path,"rb");
    if (!files[fd]) {
        enum x6_file_error error=x6_file_open_error();
        errno=error==X6_FILE_NO_MEMORY?ENOMEM:error==X6_FILE_ARGUMENT?EINVAL:ENOENT;
        return -1;
    }
    return fd;
}
int _close(int fd)
{
    if (fd<3 || fd>=FD_COUNT || !files[fd]) {errno=EBADF;return -1;}
    int result=x6_file_close(files[fd]);files[fd]=0;
    if (result) errno=EIO;return result;
}
_ssize_t _read(int fd,void *data,size_t bytes)
{
    if (fd<3 || fd>=FD_COUNT || !files[fd]) {errno=EBADF;return -1;}
    if (bytes>INT32_MAX || (!data && bytes)) {errno=EINVAL;return -1;}
    return (_ssize_t)x6_file_read(data,1,(uint32_t)bytes,files[fd]);
}
_ssize_t _write(int fd,const void *data,size_t bytes)
{
    if (fd!=1 && fd!=2) {errno=EROFS;return -1;}
    if (bytes>INT32_MAX || (!data && bytes)) {errno=EINVAL;return -1;}
    const char *text=data;
    for (size_t i=0;i<bytes;++i) log_buffer[(log_position++)%sizeof(log_buffer)]=text[i];
    return (_ssize_t)bytes;
}
_off_t _lseek(int fd,_off_t offset,int origin)
{
    if (fd<3 || fd>=FD_COUNT || !files[fd]) {errno=EBADF;return -1;}
    if (offset<INT32_MIN || offset>INT32_MAX) {errno=EOVERFLOW;return -1;}
    if (x6_file_seek(files[fd],(int32_t)offset,origin)) {errno=EIO;return -1;}
    int32_t position=x6_file_tell(files[fd]);
    if (position<0) errno=EIO;return position;
}
int _fstat(int fd,struct stat *result)
{
    if (!result) {errno=EINVAL;return -1;}
    *result=(struct stat){0};
    if (fd==1 || fd==2) {result->st_mode=S_IFCHR;result->st_nlink=1;return 0;}
    if (fd<3 || fd>=FD_COUNT || !files[fd]) {errno=EBADF;return -1;}
    int32_t position=x6_file_tell(files[fd]);
    if (position<0 || x6_file_seek(files[fd],0,2)) {errno=EIO;return -1;}
    int32_t size=x6_file_tell(files[fd]);
    int32_t restored=x6_file_seek(files[fd],position,0);
    if (size<0 || restored) {errno=EIO;return -1;}
    result->st_mode=S_IFREG|0444;result->st_nlink=1;result->st_size=size;return 0;
}
int _stat(const char *path,struct stat *result)
{
    if (!result) {errno=EINVAL;return -1;}
    int32_t size=x6_file_size(path);
    if (size<0) {errno=ENOENT;return -1;}
    *result=(struct stat){0};result->st_mode=S_IFREG|0444;result->st_nlink=1;result->st_size=size;return 0;
}
int _isatty(int fd) {if (fd==1 || fd==2) return 1;errno=ENOTTY;return 0;}
int mkdir(const char *path,mode_t mode) {(void)path;(void)mode;errno=EROFS;return -1;}
int _unlink(const char *path) {(void)path;errno=EROFS;return -1;}
int _link(const char *old_path,const char *new_path) {(void)old_path;(void)new_path;errno=EROFS;return -1;}
int _getpid(void) {errno=ENOSYS;return -1;}
int _kill(int process,int signal) {(void)process;(void)signal;errno=ENOSYS;return -1;}
int _gettimeofday(struct timeval *time,void *zone) {(void)time;(void)zone;errno=ENOSYS;return -1;}
clock_t _times(struct tms *times) {(void)times;errno=ENOSYS;return (clock_t)-1;}
void *_sbrk(ptrdiff_t increment) {(void)increment;errno=ENOMEM;return (void *)-1;}

uint32_t x6_runtime_log(char *out,uint32_t capacity)
{
    if (!out || !capacity) return 0;
    uint32_t length=log_position<sizeof(log_buffer)?log_position:(uint32_t)sizeof(log_buffer);
    if (length>=capacity) length=capacity-1;
    uint32_t start=log_position-length;
    for (uint32_t i=0;i<length;++i) out[i]=log_buffer[(start+i)%sizeof(log_buffer)];
    out[length]=0;return length;
}
