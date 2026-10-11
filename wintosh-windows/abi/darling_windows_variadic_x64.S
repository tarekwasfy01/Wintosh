.intel_syntax noprefix
.text
.globl darling_windows_abi_printf
.p2align 4
darling_windows_abi_printf:
    sub rsp, 0xa8
    mov [rsp+0x60], rdi
    mov [rsp+0x68], rsi
    mov [rsp+0x70], rdx
    mov [rsp+0x78], rcx
    mov [rsp+0x80], r8
    mov [rsp+0x88], r9
    mov dword ptr [rsp+0x40], 8
    mov dword ptr [rsp+0x44], 48
    lea rax, [rsp+0xb0]
    mov [rsp+0x48], rax
    lea rax, [rsp+0x60]
    mov [rsp+0x50], rax
    mov rcx, [rsp+0x60]
    lea rdx, [rsp+0x40]
    call darling_windows_abi_printf_windows_helper
    add rsp, 0xa8
    ret
