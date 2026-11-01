#ifndef X6_GUARD_H
#define X6_GUARD_H
#include <stdint.h>
/* Used only by the ARM/newlib build. Fatal libc/core exits unwind to a guarded
 * frontend call rather than pretending the failed operation succeeded. */
int32_t x6_guard_call(int32_t (*function)(void *), void *context);
int32_t x6_stack_guarded(int32_t (*function)(void *), void *context);
uint32_t x6_private_stack_used(void);
_Noreturn void x6_runtime_abort(int status);
#endif
