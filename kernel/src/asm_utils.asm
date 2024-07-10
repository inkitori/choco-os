SECTION .text

global spinlock
global hcf 

spinlock:
    hlt
    jmp spinlock

hcf:
    cli
    hcf_loop:
    hlt
    jmp hcf_loop