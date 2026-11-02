/* Read-only filesystem wrappers matching the stock ARM service-call pattern.
 * These service numbers are version-specific to the inspected Melis baseline. */
.syntax unified
.cpu arm926ej-s
.arm

.section .text.x6_fs_open_raw,"ax",%progbits
.global x6_fs_open_raw
.type x6_fs_open_raw,%function
x6_fs_open_raw:
    push {r4, lr}
    svc #0x2602
    pop {r4, pc}
.size x6_fs_open_raw, .-x6_fs_open_raw

.section .text.x6_fs_read,"ax",%progbits
.global x6_fs_read
.type x6_fs_read,%function
x6_fs_read:
    push {r4, lr}
    svc #0x2604
    pop {r4, pc}
.size x6_fs_read, .-x6_fs_read

.section .text.x6_fs_close,"ax",%progbits
.global x6_fs_close
.type x6_fs_close,%function
x6_fs_close:
    push {r4, lr}
    svc #0x2603
    pop {r4, pc}
.size x6_fs_close, .-x6_fs_close

/* Seek/tell: stock wrappers e9100678/e9100684; offsets are signed 32-bit. */
.section .text.x6_fs_seek,"ax",%progbits
.global x6_fs_seek
.type x6_fs_seek,%function
x6_fs_seek:
    push {r4, lr}
    svc #0x2606
    pop {r4, pc}
.size x6_fs_seek, .-x6_fs_seek

.section .text.x6_fs_tell,"ax",%progbits
.global x6_fs_tell
.type x6_fs_tell,%function
x6_fs_tell:
    push {r4, lr}
    svc #0x2607
    pop {r4, pc}
.size x6_fs_tell, .-x6_fs_tell

/* Heap-zero wrappers follow the argument shuffles in stock fba.mod at
 * e91031ac (alloc), e91031c0 (free), and e91031d8 (resize). */
.section .text.x6_heap_alloc,"ax",%progbits
.global x6_heap_alloc
.type x6_heap_alloc,%function
x6_heap_alloc:
    mov r1, r0
    mov r0, #0
    push {r4, lr}
    svc #0x2507
    pop {r4, pc}
.size x6_heap_alloc, .-x6_heap_alloc

.section .text.x6_heap_free,"ax",%progbits
.global x6_heap_free
.type x6_heap_free,%function
x6_heap_free:
    mov r1, r0
    mov r0, #0
    push {r4, lr}
    svc #0x2508
    pop {r4, pc}
.size x6_heap_free, .-x6_heap_free

.section .text.x6_heap_resize,"ax",%progbits
.global x6_heap_resize
.type x6_heap_resize,%function
x6_heap_resize:
    mov r2, r1
    mov r1, r0
    mov r0, #0
    push {r4, lr}
    svc #0x2509
    pop {r4, pc}
.size x6_heap_resize, .-x6_heap_resize

/* fwrite(ptr, size, count, fp): same service the stock shell uses (shell.zgj 0xf464). */
.section .text.x6_fs_write,"ax",%progbits
.global x6_fs_write
.type x6_fs_write,%function
x6_fs_write:
    push {r4, lr}
    svc #0x2605
    pop {r4, pc}
.size x6_fs_write, .-x6_fs_write
