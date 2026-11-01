/* Stock e9103088-309c rounds bytes to 1 KiB pages and calls 2500(pages,0).
 * Stock e9103118-311c releases the same pointer with 2501(pointer,0). */
.syntax unified
.cpu arm926ej-s
.arm
.section .text.x6_page_alloc,"ax",%progbits
.global x6_page_alloc
.type x6_page_alloc,%function
x6_page_alloc:
    push {r4,lr}
    mov r1,#0
    svc #0x2500
    pop {r4,pc}
.size x6_page_alloc,.-x6_page_alloc
.section .text.x6_page_free,"ax",%progbits
.global x6_page_free
.type x6_page_free,%function
x6_page_free:
    push {r4,lr}
    mov r1,#0
    svc #0x2501
    pop {r4,pc}
.size x6_page_free,.-x6_page_free
