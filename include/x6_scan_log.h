#ifndef X6_SCAN_LOG_H
#define X6_SCAN_LOG_H
#include "x6_library.h"
enum x6_scan_log_status {X6_LOG_UNKNOWN, X6_LOG_SAVED, X6_LOG_OPEN_FAILED, X6_LOG_IO_FAILED};
/* Bounded last-boot log. Each important checkpoint is closed to persist it.
 * Logging failure never interrupts discovery or changes the scan error. */
uint32_t x6_scan_log_begin(void);
uint32_t x6_scan_log_checkpoint(const struct x6_scan_info *scan);
void x6_scan_log_io(const char *operation, const char *path, int32_t result,
                    uint32_t expected, const struct x6_scan_info *scan);
void x6_scan_log_failure(const struct x6_scan_info *scan, uint32_t flag,
                         int32_t code, const char *path);
#endif
