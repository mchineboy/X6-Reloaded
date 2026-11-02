/* Closed checkpoints survive an early scan return, or identify the last stage
 * entered if a later operation hangs. No ROM contents or save data are logged. */
#include "x6_scan_log.h"
#include "x6_runtime.h"
#define LOG_PATH "F:/x6/SCAN.LOG"
#define CAPACITY 65536u
#define RESERVED 2048u
static char buffer[CAPACITY], last_directory[96];
static uint32_t used, sequence, log_status, disabled, last_phase, last_library, last_checked, last_errors;
static uint32_t last_written, last_expected, retried;
static int32_t last_closed;
static void text(const char *p)
{
    while (p && *p && used + 1 < CAPACITY) {
        unsigned char c = (unsigned char)*p++;
        buffer[used++] = c < 32 && c != '\n' && c != '\t' ? '?' : (char)c;
    }
    buffer[used] = 0;
}
static void value(uint32_t n)
{
    char digits[12];uint32_t count = 0;
    do {digits[count++] = (char)('0' + n % 10);n /= 10;} while (n);
    while (count) {char c[2] = {digits[--count], 0};text(c);}
}
static void signed_value(int32_t n)
{
    if (n < 0) {text("-");value(0u - (uint32_t)n);} else value((uint32_t)n);
}
static const char *phase(uint32_t n)
{
    static const char *const names[] = {"preparing", "titles", "cache-open", "scanning",
        "cache-close", "sorting", "ready", "failed", "opening-menu", "loading-saved"};
    return n <= X6_SCAN_CACHED ? names[n] : "unknown";
}
static void prefix(const char *event, const struct x6_scan_info *p)
{
    text("seq=");value(++sequence);text(" event=");text(event);
    if (p) {
        text(" phase=");text(phase(p->phase));text(" library=");text(x6_library_name(p->library));
        text(" checked=");value(p->checked);text(" indexed=");value(p->indexed);
        text(" flags=");value(p->errors);
        text(" cache_hit=");value(p->cache_hit);
    }
}
static void flush(void)
{
    if (disabled) return;
    void *f = x6_fs_open(LOG_PATH, "wb");
    if (!f) {log_status = X6_LOG_OPEN_FAILED;disabled = 1;return;}
    uint32_t written = x6_fs_write(buffer, 1, used, f);
    int32_t closed = x6_fs_close(f);
    last_written = written;last_expected = used;last_closed = closed;
    if (written != used || closed != 0) {log_status = X6_LOG_IO_FAILED;disabled = 1;}
    else log_status = X6_LOG_SAVED;
}
uint32_t x6_scan_log_begin(void)
{
    used = sequence = disabled = last_checked = last_errors = 0;
    last_written = last_expected = retried = 0;last_closed = 0;
    last_phase = last_library = 0xffffffffu;last_directory[0] = 0;log_status = X6_LOG_UNKNOWN;
    text("X6 Reloaded scan log v1\nLast boot only; bounded to 64 KiB.\n");
    text("Cache: F:/x6/LIBRARY.IDX\nCodes are application/operation results, not OS errno.\n");
    text("ROM contents and save data are not recorded.\n");flush();return log_status;
}
static uint32_t same(const char *a, const char *b)
{
    while (*a && *a == *b) {++a;++b;}return *a == *b;
}
uint32_t x6_scan_log_checkpoint(const struct x6_scan_info *p)
{
    if (!p) return log_status;
    uint32_t terminal = p->phase == X6_SCAN_FAILED || p->phase == X6_SCAN_READY;
    uint32_t changed = p->phase != last_phase || p->library != last_library
        || !same(p->directory, last_directory) || p->errors != last_errors;
    if (!changed && p->checked - last_checked < 1024u) return log_status;
    last_phase = p->phase;last_library = p->library;last_checked = p->checked;last_errors = p->errors;
    uint32_t i = 0;for (; p->directory[i] && i + 1 < sizeof last_directory; ++i) last_directory[i] = p->directory[i];
    last_directory[i] = 0;
    if (used + 768u + (terminal ? 0u : RESERVED) >= CAPACITY) return log_status;
    prefix("checkpoint", p);text(" directory=");text(p->directory);
    if (p->errors) {text(" first_error=");signed_value(p->error_code);text(" error_path=");text(p->error_path);}
    text("\n");
    /* One terminal retry can retain the accumulated failure even when an
       earlier log write/close failed. Never repeatedly accumulate handles. */
    if (terminal && disabled && !retried && used + 256 < CAPACITY) {
        prefix("log-final-retry", p);text(" previous_write=");value(last_written);
        text(" previous_expected=");value(last_expected);text(" previous_close=");signed_value(last_closed);text("\n");
        disabled = 0;retried = 1;
    }
    flush();return log_status;
}
void x6_scan_log_io(const char *operation, const char *path, int32_t result,
                    uint32_t expected, const struct x6_scan_info *p)
{
    if (used + 768u + RESERVED >= CAPACITY) return;
    prefix("io", p);text(" operation=");text(operation);text(" result=");signed_value(result);
    text(" expected=");value(expected);text(" path=");text(path);text("\n");flush();
}
void x6_scan_log_failure(const struct x6_scan_info *p, uint32_t flag, int32_t code, const char *path)
{
    if (used + 768u >= CAPACITY) return;
    prefix("error", p);text(" error_flag=");value(flag);text(" operation_code=");signed_value(code);
    text(" path=");text(path);text("\n");flush();
}
