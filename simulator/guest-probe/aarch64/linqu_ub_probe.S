.section __TEXT,__text,regular,pure_instructions
.globl _start
.align 2

.equ UART0_BASE,         0x09000000
.equ UARTFR,             0x18
.equ UARTDR,             0x00

.equ LINQU_BASE,         0x0c000000
.equ EP1_BASE,           0x0c001000
.equ REG_VERSION,        0x000
.equ REG_CMDQ_BASE_LO,   0x010
.equ REG_CMDQ_BASE_HI,   0x018
.equ REG_CMDQ_SIZE,      0x020
.equ REG_CMDQ_HEAD,      0x028
.equ REG_CMDQ_TAIL,      0x030
.equ REG_CQ_BASE_LO,     0x038
.equ REG_CQ_BASE_HI,     0x040
.equ REG_CQ_SIZE,        0x048
.equ REG_CQ_HEAD,        0x050
.equ REG_CQ_TAIL,        0x058
.equ REG_STATUS,         0x060
.equ REG_DOORBELL,       0x068
.equ REG_LAST_ERROR,     0x070
.equ REG_IRQ_STATUS,     0x078

.equ CMDQ_ADDR,          0x40210000
.equ CQ_ADDR,            0x40220000

_start:
    adrp x20, probe_banner@PAGE
    add  x20, x20, probe_banner@PAGEOFF
    bl   uart_puts
    bl   uart_newline

    movz x0, #(LINQU_BASE >> 16), lsl #16
    movk x0, #(LINQU_BASE & 0xffff), lsl #0
    ldr  x1, [x0, #REG_VERSION]
    adrp x20, version_label@PAGE
    add  x20, x20, version_label@PAGEOFF
    bl   uart_puts
    mov  x0, x1
    bl   uart_puthex64
    bl   uart_newline

    adrp x20, step_build_desc@PAGE
    add  x20, x20, step_build_desc@PAGEOFF
    bl   uart_puts
    bl   uart_newline
    bl   build_descriptor

    adrp x20, step_zero_cq@PAGE
    add  x20, x20, step_zero_cq@PAGEOFF
    bl   uart_puts
    bl   uart_newline
    bl   zero_cq_slot

    adrp x20, step_program_regs@PAGE
    add  x20, x20, step_program_regs@PAGEOFF
    bl   uart_puts
    bl   uart_newline
    bl   program_registers

    adrp x20, step_ring@PAGE
    add  x20, x20, step_ring@PAGEOFF
    bl   uart_puts
    bl   uart_newline
    bl   ring_device

    adrp x20, step_dump@PAGE
    add  x20, x20, step_dump@PAGEOFF
    bl   uart_puts
    bl   uart_newline
    bl   dump_results

hang:
    wfe
    b    hang

build_descriptor:
    movz x9, #(CMDQ_ADDR >> 16), lsl #16
    movk x9, #(CMDQ_ADDR & 0xffff), lsl #0

    mov x10, #0
    str x10, [x9, #0]
    str x10, [x9, #8]
    str x10, [x9, #16]
    str x10, [x9, #24]
    str x10, [x9, #32]
    str x10, [x9, #40]
    str x10, [x9, #48]
    str x10, [x9, #56]

    mov w10, #1                  // kind = Io
    strb w10, [x9, #0]
    mov w10, #7                  // op_id low byte
    strb w10, [x9, #1]
    mov w10, #2                  // WriteBlock
    strb w10, [x9, #14]
    mov w10, #0                  // no segment
    strb w10, [x9, #15]
    mov w10, #1                  // block present
    strb w10, [x9, #16]
    mov w10, #4                  // len("blk0")
    strb w10, [x9, #17]
    mov w10, #'b'
    strb w10, [x9, #18]
    mov w10, #'l'
    strb w10, [x9, #19]
    mov w10, #'k'
    strb w10, [x9, #20]
    mov w10, #'0'
    strb w10, [x9, #21]
    ret

zero_cq_slot:
    movz x9, #(CQ_ADDR >> 16), lsl #16
    movk x9, #(CQ_ADDR & 0xffff), lsl #0
    mov x10, #0
    str x10, [x9, #0]
    str x10, [x9, #8]
    str x10, [x9, #16]
    str x10, [x9, #24]
    str x10, [x9, #32]
    str x10, [x9, #40]
    str x10, [x9, #48]
    str x10, [x9, #56]
    ret

program_registers:
    movz x9, #(EP1_BASE >> 16), lsl #16
    movk x9, #(EP1_BASE & 0xffff), lsl #0

    movz x10, #(CMDQ_ADDR >> 16), lsl #16
    movk x10, #(CMDQ_ADDR & 0xffff), lsl #0
    str  x10, [x9, #REG_CMDQ_BASE_LO]
    mov  x10, #0
    str  x10, [x9, #REG_CMDQ_BASE_HI]
    mov  x10, #32
    str  x10, [x9, #REG_CMDQ_SIZE]

    movz x10, #(CQ_ADDR >> 16), lsl #16
    movk x10, #(CQ_ADDR & 0xffff), lsl #0
    str  x10, [x9, #REG_CQ_BASE_LO]
    mov  x10, #0
    str  x10, [x9, #REG_CQ_BASE_HI]
    mov  x10, #64
    str  x10, [x9, #REG_CQ_SIZE]
    mov  x10, #0
    str  x10, [x9, #REG_CMDQ_HEAD]
    str  x10, [x9, #REG_CQ_HEAD]
    ret

ring_device:
    movz x9, #(EP1_BASE >> 16), lsl #16
    movk x9, #(EP1_BASE & 0xffff), lsl #0
    mov  x10, #1
    str  x10, [x9, #REG_CMDQ_TAIL]
    str  x10, [x9, #REG_DOORBELL]
    ret

dump_results:
    movz x9, #(EP1_BASE >> 16), lsl #16
    movk x9, #(EP1_BASE & 0xffff), lsl #0
    movz x19, #(CQ_ADDR >> 16), lsl #16
    movk x19, #(CQ_ADDR & 0xffff), lsl #0

    adrp x20, status_label@PAGE
    add  x20, x20, status_label@PAGEOFF
    bl   uart_puts
    ldr  x0, [x9, #REG_STATUS]
    bl   uart_puthex64
    bl   uart_newline

    adrp x20, cq_tail_label@PAGE
    add  x20, x20, cq_tail_label@PAGEOFF
    bl   uart_puts
    ldr  x0, [x9, #REG_CQ_TAIL]
    bl   uart_puthex64
    bl   uart_newline

    adrp x20, cmdq_head_label@PAGE
    add  x20, x20, cmdq_head_label@PAGEOFF
    bl   uart_puts
    ldr  x0, [x9, #REG_CMDQ_HEAD]
    bl   uart_puthex64
    bl   uart_newline

    adrp x20, cmdq_tail_label@PAGE
    add  x20, x20, cmdq_tail_label@PAGEOFF
    bl   uart_puts
    ldr  x0, [x9, #REG_CMDQ_TAIL]
    bl   uart_puthex64
    bl   uart_newline

    adrp x20, irq_label@PAGE
    add  x20, x20, irq_label@PAGEOFF
    bl   uart_puts
    ldr  x0, [x9, #REG_IRQ_STATUS]
    bl   uart_puthex64
    bl   uart_newline

    adrp x20, last_error_label@PAGE
    add  x20, x20, last_error_label@PAGEOFF
    bl   uart_puts
    ldr  x0, [x9, #REG_LAST_ERROR]
    bl   uart_puthex64
    bl   uart_newline

    adrp x20, cq_slot_label@PAGE
    add  x20, x20, cq_slot_label@PAGEOFF
    bl   uart_puts
    ldr  x0, [x19, #0]
    bl   uart_puthex64
    bl   uart_newline
    ret

uart_newline:
    mov x21, x30
    mov w0, #'\r'
    bl uart_putc
    mov w0, #'\n'
    bl uart_putc
    mov x30, x21
    ret

uart_puts:
    mov x21, x30
1:
    ldrb w0, [x20], #1
    cbz  w0, 2f
    bl   uart_putc
    b    1b
2:
    mov x30, x21
    ret

uart_puthex64:
    mov x21, x30
    mov x2, x0
    mov x3, #16
2:
    lsr x1, x2, #60
    cmp x1, #10
    b.lo 3f
    add x1, x1, #'a' - 10
    b   4f
3:
    add x1, x1, #'0'
4:
    mov x0, x1
    bl  uart_putc
    lsl x2, x2, #4
    subs x3, x3, #1
    b.ne 2b
    mov x30, x21
    ret

uart_putc:
    movz x1, #(UART0_BASE >> 16), lsl #16
    movk x1, #(UART0_BASE & 0xffff), lsl #0
5:
    ldr w2, [x1, #UARTFR]
    tst w2, #(1 << 5)
    b.ne 5b
    strb w0, [x1, #UARTDR]
    ret

.section __TEXT,__cstring,cstring_literals
probe_banner:
    .asciz "linqu-ub guest probe"
version_label:
    .asciz "version="
status_label:
    .asciz "status="
cq_tail_label:
    .asciz "cq_tail="
cmdq_head_label:
    .asciz "cmdq_head="
cmdq_tail_label:
    .asciz "cmdq_tail="
irq_label:
    .asciz "irq_status="
last_error_label:
    .asciz "last_error="
cq_slot_label:
    .asciz "cq_slot0="
step_build_desc:
    .asciz "step=build_descriptor"
step_zero_cq:
    .asciz "step=zero_cq"
step_program_regs:
    .asciz "step=program_registers"
step_ring:
    .asciz "step=ring_device"
step_dump:
    .asciz "step=dump_results"
