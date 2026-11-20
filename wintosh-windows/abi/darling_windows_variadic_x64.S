/*
 * Wintosh-original source. Copyright (c) 2026 Tarek Wasfy.
 * SPDX-License-Identifier: MIT
 * See ../../LICENSE-WINTOSH-ORIGINAL-MIT.txt.
 */
.intel_syntax noprefix
.text
.globl darling_windows_abi_printf
.p2align 4
darling_windows_abi_printf:
        sub rsp, 0x128
    mov [rsp+0x60], rdi
    mov [rsp+0x68], rsi
    mov [rsp+0x70], rdx
    mov [rsp+0x78], rcx
    mov [rsp+0x80], r8
    mov [rsp+0x88], r9
        movdqu [rsp+0x90], xmm0
        movdqu [rsp+0xa0], xmm1
        movdqu [rsp+0xb0], xmm2
        movdqu [rsp+0xc0], xmm3
        movdqu [rsp+0xd0], xmm4
        movdqu [rsp+0xe0], xmm5
        movdqu [rsp+0xf0], xmm6
        movdqu [rsp+0x100], xmm7
    mov dword ptr [rsp+0x40], 8
    mov dword ptr [rsp+0x44], 48
        lea rax, [rsp+0x130]
    mov [rsp+0x48], rax
    lea rax, [rsp+0x60]
    mov [rsp+0x50], rax
    mov rcx, [rsp+0x60]
    lea rdx, [rsp+0x40]
    call darling_windows_abi_printf_windows_helper
        add rsp, 0x128
    ret

/* Raw scalar-only forwarding for registered guest IMPs whose signatures do
   not need a special aggregate or mixed-layout adapter. Arguments are restored
   from DarlingSysVObjcCallFrame and overflow words are copied from the saved
   SysV stack area. The helper itself uses the Windows x64 calling convention. */
.globl darling_windows_objc_invoke_sysv_raw
.p2align 4
darling_windows_objc_invoke_sysv_raw:
    push rbx
    push rbp
    push rdi
    push rsi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 168
    movdqu [rsp+0], xmm6
    movdqu [rsp+16], xmm7
    movdqu [rsp+32], xmm8
    movdqu [rsp+48], xmm9
    movdqu [rsp+64], xmm10
    movdqu [rsp+80], xmm11
    movdqu [rsp+96], xmm12
    movdqu [rsp+112], xmm13
    movdqu [rsp+128], xmm14
    movdqu [rsp+144], xmm15
    mov r12, rcx
    mov r13, rdx
    mov rbx, r8
    mov r14, rsp
    mov r15, r14
    sub r15, rbx
    sub r15, 16
    and r15, -16
    add r15, 8
    lea rax, [rip+darling_windows_objc_invoke_sysv_raw_return]
    mov [r15], rax
    test rbx, rbx
    jz darling_windows_objc_invoke_sysv_raw_args_copied
    lea rdi, [r15+8]
    mov rsi, [r12+176]
    mov rcx, rbx
    rep movsb
darling_windows_objc_invoke_sysv_raw_args_copied:
    mov rdi, [r12+0]
    mov rsi, [r12+8]
    mov rdx, [r12+16]
    mov rcx, [r12+24]
    mov r8, [r12+32]
    mov r9, [r12+40]
    movdqu xmm0, [r12+48]
    movdqu xmm1, [r12+64]
    movdqu xmm2, [r12+80]
    movdqu xmm3, [r12+96]
    movdqu xmm4, [r12+112]
    movdqu xmm5, [r12+128]
    movdqu xmm6, [r12+144]
    movdqu xmm7, [r12+160]
    mov al, byte ptr [r12+184]
    mov rsp, r15
    jmp r13
darling_windows_objc_invoke_sysv_raw_return:
    mov r10, rax
    movdqu [r12+192], xmm0
    mov [r12+200], rdx
    mov rsp, r14
    movdqu xmm6, [rsp+0]
    movdqu xmm7, [rsp+16]
    movdqu xmm8, [rsp+32]
    movdqu xmm9, [rsp+48]
    movdqu xmm10, [rsp+64]
    movdqu xmm11, [rsp+80]
    movdqu xmm12, [rsp+96]
    movdqu xmm13, [rsp+112]
    movdqu xmm14, [rsp+128]
    movdqu xmm15, [rsp+144]
    add rsp, 168
    pop r15
    pop r14
    pop r13
    pop r12
    pop rsi
    pop rdi
    pop rbp
    pop rbx
    mov rax, r10
    ret

/* SysV variadic Objective-C entry points forward a captured frame to Windows
   ABI C++ dispatchers. See DarlingSysVObjcCallFrame in darling_windows_objc.cpp. */
.globl darling_windows_abi_objc_msgSend_sysv
.p2align 4
darling_windows_abi_objc_msgSend_sysv:
    sub rsp, 248
    movzx eax, al
    mov [rsp+216], rax
    mov [rsp+32], rdi
    mov [rsp+40], rsi
    mov [rsp+48], rdx
    mov [rsp+56], rcx
    mov [rsp+64], r8
    mov [rsp+72], r9
    movdqu [rsp+80], xmm0
    movdqu [rsp+96], xmm1
    movdqu [rsp+112], xmm2
    movdqu [rsp+128], xmm3
    movdqu [rsp+144], xmm4
    movdqu [rsp+160], xmm5
    movdqu [rsp+176], xmm6
    movdqu [rsp+192], xmm7
    lea rax, [rsp+256]
    mov [rsp+208], rax
    pxor xmm0, xmm0
    movdqu [rsp+224], xmm0
    lea rcx, [rsp+32]
    call darling_windows_objc_msgSend_sysv_dispatch
    movq xmm0, [rsp+224]
    movq xmm1, [rsp+232]
    mov rdx, [rsp+232]
    add rsp, 248
    ret

.globl darling_windows_abi_objc_msgSendSuper_sysv
.p2align 4
darling_windows_abi_objc_msgSendSuper_sysv:
    sub rsp, 248
    movzx eax, al
    mov [rsp+216], rax
    mov [rsp+32], rdi
    mov [rsp+40], rsi
    mov [rsp+48], rdx
    mov [rsp+56], rcx
    mov [rsp+64], r8
    mov [rsp+72], r9
    movdqu [rsp+80], xmm0
    movdqu [rsp+96], xmm1
    movdqu [rsp+112], xmm2
    movdqu [rsp+128], xmm3
    movdqu [rsp+144], xmm4
    movdqu [rsp+160], xmm5
    movdqu [rsp+176], xmm6
    movdqu [rsp+192], xmm7
    lea rax, [rsp+256]
    mov [rsp+208], rax
    pxor xmm0, xmm0
    movdqu [rsp+224], xmm0
    lea rcx, [rsp+32]
    call darling_windows_objc_msgSendSuper_sysv_dispatch
    movq xmm0, [rsp+224]
    movq xmm1, [rsp+232]
    mov rdx, [rsp+232]
    add rsp, 248
    ret

.globl darling_windows_abi_objc_msgSendSuper2_sysv
.p2align 4
darling_windows_abi_objc_msgSendSuper2_sysv:
    sub rsp, 248
    movzx eax, al
    mov [rsp+216], rax
    mov [rsp+32], rdi
    mov [rsp+40], rsi
    mov [rsp+48], rdx
    mov [rsp+56], rcx
    mov [rsp+64], r8
    mov [rsp+72], r9
    movdqu [rsp+80], xmm0
    movdqu [rsp+96], xmm1
    movdqu [rsp+112], xmm2
    movdqu [rsp+128], xmm3
    movdqu [rsp+144], xmm4
    movdqu [rsp+160], xmm5
    movdqu [rsp+176], xmm6
    movdqu [rsp+192], xmm7
    lea rax, [rsp+256]
    mov [rsp+208], rax
    pxor xmm0, xmm0
    movdqu [rsp+224], xmm0
    lea rcx, [rsp+32]
    call darling_windows_objc_msgSendSuper2_sysv_dispatch
    movq xmm0, [rsp+224]
    movq xmm1, [rsp+232]
    mov rdx, [rsp+232]
    add rsp, 248
    ret
