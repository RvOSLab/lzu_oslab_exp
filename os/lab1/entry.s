    .globl boot_stack, boot_stack_top, _start
    .section .text.entry
_start:
    // TODO：修改 栈指针(sp) 为 boot_stack_top
    // TODO：跳转到 main 函数

    .section .bss
boot_stack:
    .space 4096 * 16
boot_stack_top:
