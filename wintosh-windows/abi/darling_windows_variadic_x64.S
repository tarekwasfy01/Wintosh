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
