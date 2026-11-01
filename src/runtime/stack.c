/* Dedicated stack for the deeper MAME/Newlib call chain. No RTOS thread or
 * extra SVC is introduced. Re-entry from a different stack is rejected. */
#include <stdint.h>
#include "x6_guard.h"
#define WORDS (65536u / 4u)
#define GUARDS 8u
#define UNUSED 0xa5a5a5a5u
#define CANARY 0xc0def00du
static volatile uint32_t stack_words[WORDS] __attribute__((aligned(8)));
static volatile uint32_t stack_in_use;
static uint32_t stack_ready;
extern int32_t x6_switch_stack(int32_t (*function)(void *),void *context,void *top);
extern uint32_t x6_stack_try_lock(volatile uint32_t *lock);
struct guarded_arguments {int32_t (*function)(void *);void *context;};
static int32_t invoke_guard(void *context)
{
    struct guarded_arguments *args=context;
    return x6_guard_call(args->function,args->context);
}
int32_t x6_stack_guarded(int32_t (*function)(void *),void *context)
{
    uintptr_t current;
    __asm__ volatile("mov %0, sp":"=r"(current));
    if (current>(uintptr_t)(stack_words+GUARDS) && current<=(uintptr_t)(stack_words+WORDS))
        return x6_guard_call(function,context);
    if (x6_stack_try_lock(&stack_in_use)) return -126;
    if (!stack_ready) {
        for (uint32_t i=0;i<WORDS;++i) stack_words[i]=i<GUARDS?CANARY:UNUSED;
        stack_ready=1;
    }
    struct guarded_arguments args={function,context};
    int32_t result=x6_switch_stack(invoke_guard,&args,(void *)(stack_words+WORDS));
    for (uint32_t i=0;i<GUARDS;++i) if (stack_words[i]!=CANARY) result=-125;
    __asm__ volatile("":::"memory");stack_in_use=0;
    return result;
}
uint32_t x6_private_stack_used(void)
{
    if (!stack_ready) return 0;
    uint32_t first=GUARDS;
    while (first<WORDS && stack_words[first]==UNUSED) ++first;
    return (WORDS-first)*4u;
}
