.syntax unified
.cpu arm926ej-s
.arm
.section .text.x6_switch_stack,"ax",%progbits
.global x6_switch_stack
.type x6_switch_stack,%function
x6_switch_stack:
    push {r4, r5, r6, lr}
    mov r4, sp
    mov r5, r0
    mov r0, r1
    mov sp, r2
    blx r5
    mov sp, r4
    pop {r4, r5, r6, pc}
.size x6_switch_stack, .-x6_switch_stack

/* ARM926 has SWP, not LDREX/STREX. Acquire the one-word re-entry gate atomically.
 * Both the lock and its users are private to this single-core module. */
.section .text.x6_stack_try_lock,"ax",%progbits
.global x6_stack_try_lock
.type x6_stack_try_lock,%function
x6_stack_try_lock:
    mov r2, r0
    mov r1, #1
    swp r0, r1, [r2]
    bx lr
.size x6_stack_try_lock, .-x6_stack_try_lock
