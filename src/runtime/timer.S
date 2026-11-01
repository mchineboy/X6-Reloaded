/* Version-specific timer services observed in the stock FBA and launcher.
 * Get: stock FBA e918de90; delay: launcher 62d90, used by its audio path.
 * No clock-setting, device-write or flash service is imported. */
.syntax unified
.cpu arm926ej-s
.arm
.section .text.x6_time_ticks,"ax",%progbits
.global x6_time_ticks
.type x6_time_ticks,%function
x6_time_ticks:
    push {r4, lr}
    svc #0x241f
    pop {r4, pc}
.size x6_time_ticks, .-x6_time_ticks
.section .text.x6_time_delay,"ax",%progbits
.global x6_time_delay
.type x6_time_delay,%function
x6_time_delay:
    push {r4, lr}
    svc #0x241c
    pop {r4, pc}
.size x6_time_delay, .-x6_time_delay
