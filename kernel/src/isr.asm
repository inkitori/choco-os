SECTION .text
extern exception_handler
global trigger_test_interrupt

trigger_test_interrupt:
    int 0xD

%macro pushaq 0
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
%endmacro

%macro popaq 0
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
%endmacro

extern keyboard_handler
extern timer_handler

isr_timer:
    pushaq
    call timer_handler
    popaq
    iretq

isr_keyboard:
    pushaq
    call keyboard_handler
    popaq
    iretq

%macro isr_err_stub 1
isr_stub_%+%1:
	mov rdi, %+%1
    call exception_handler
    iretq
%endmacro
; if writing for 64-bit, use iretq instead
%macro isr_no_err_stub 1
isr_stub_%+%1:
	mov rdi, %+%1
    call exception_handler
    iretq
%endmacro

isr_no_err_stub 0
isr_no_err_stub 1
isr_no_err_stub 2
isr_no_err_stub 3
isr_no_err_stub 4
isr_no_err_stub 5
isr_no_err_stub 6
isr_no_err_stub 7
isr_err_stub    8
isr_no_err_stub 9
isr_err_stub    10
isr_err_stub    11
isr_err_stub    12
isr_err_stub    13
isr_err_stub    14
isr_no_err_stub 15
isr_no_err_stub 16
isr_err_stub    17
isr_no_err_stub 18
isr_no_err_stub 19
isr_no_err_stub 20
isr_no_err_stub 21
isr_no_err_stub 22
isr_no_err_stub 23
isr_no_err_stub 24
isr_no_err_stub 25
isr_no_err_stub 26
isr_no_err_stub 27
isr_no_err_stub 28
isr_no_err_stub 29
isr_err_stub    30
isr_no_err_stub 31
isr_no_err_stub 32 ; keyboard
isr_no_err_stub 33

SECTION .data
global isr_stub_table
isr_stub_table:
%assign i 0
%rep    32
    dq isr_stub_%+i 
%assign i i+1 
%endrep
dq isr_timer
dq isr_keyboard