@ Runs after boot1 teardown: replace the RAM ramdisk (0x80c00000, 307200 bytes)
@ with the image the host staged at 0x81100000, report on UART1, start the kernel.
    .arm
    .global _start
_start:
    ldr   r0, =0x80c00000
    ldr   r1, =0x81100000
    ldr   r2, =307200
1:  ldr   r3, [r1], #4
    str   r3, [r0], #4
    subs  r2, r2, #4
    bne   1b
    adr   r4, msg
2:  ldrb  r0, [r4], #1
    cmp   r0, #0
    beq   3f
    ldr   r1, =0x01c25400
4:  ldr   r12, [r1, #0x14]
    tst   r12, #0x20
    beq   4b
    str   r0, [r1]
    b     2b
3:  mov   r0, #0x80000000
    ldr   pc, =0x80411378
msg: .asciz "\r\n[X6STUB] ramdisk replaced\r\n"
    .ltorg
