/*
 * Wintosh-original x86_64 Darwin stub-trampoline probe.
 * SPDX-License-Identifier: MIT
 * See ../../LICENSE-WINTOSH-ORIGINAL-MIT.txt.
 *
 * The stack metadata offsets match the documented Darwin dyld binder frame:
 * rbp+8 = image-loader cache, rbp+16 = lazy-binding-info, rbp+24 = return.
 * This probe is not installed as dyld_stub_binder.
 */
.intel_syntax noprefix
.text

.globl darling_windows_darwin_stub_trampoline_probe
.globl darling_windows_abi_dyld_stub_binder
.p2align 4
darling_windows_abi_dyld_stub_binder:
darling_windows_darwin_stub_trampoline_probe:
    push rbp
    mov rbp, rsp
    sub rsp, 0x100

    mov [rsp+0x00], rax
    mov [rsp+0x08], rbx
    mov [rsp+0x10], rcx
    mov [rsp+0x18], rdx
    mov [rsp+0x20], rsi
    mov [rsp+0x28], rdi
    mov [rsp+0x30], r8
    mov [rsp+0x38], r9
    mov [rsp+0x40], r10
    movdqu [rsp+0x50], xmm0
    movdqu [rsp+0x60], xmm1
    movdqu [rsp+0x70], xmm2
    movdqu [rsp+0x80], xmm3
    movdqu [rsp+0x90], xmm4
    movdqu [rsp+0xA0], xmm5
    movdqu [rsp+0xB0], xmm6
    movdqu [rsp+0xC0], xmm7

    mov rdi, [rbp+8]
    mov rsi, [rbp+16]
    call darling_windows_darwin_stub_probe_resolver
    mov r11, rax
    test r11, r11
    jz darling_windows_abi_dyld_stub_binder_fail_closed

    mov rax, [rsp+0x00]
    mov rbx, [rsp+0x08]
    mov rcx, [rsp+0x10]
    mov rdx, [rsp+0x18]
    mov rsi, [rsp+0x20]
    mov rdi, [rsp+0x28]
    mov r8, [rsp+0x30]
    mov r9, [rsp+0x38]
    mov r10, [rsp+0x40]
    movdqu xmm0, [rsp+0x50]
    movdqu xmm1, [rsp+0x60]
    movdqu xmm2, [rsp+0x70]
    movdqu xmm3, [rsp+0x80]
    movdqu xmm4, [rsp+0x90]
    movdqu xmm5, [rsp+0xA0]
    movdqu xmm6, [rsp+0xB0]
    movdqu xmm7, [rsp+0xC0]

    mov rsp, rbp
    pop rbp
    add rsp, 16
    jmp r11

.globl darling_windows_darwin_stub_trampoline_fixture
.p2align 4
darling_windows_darwin_stub_trampoline_fixture:
    /* Preserve the caller return address at the metadata frame's +16. */
    sub rsp, 16
    mov qword ptr [rsp+0x00], 0x9000
    mov qword ptr [rsp+0x08], rdx
    jmp darling_windows_darwin_stub_trampoline_probe

.section .drectve,"dr"
.ascii " -export:darling_windows_darwin_stub_trampoline_probe"
.ascii " -export:darling_windows_darwin_stub_trampoline_fixture"
.ascii " -export:darling_windows_abi_dyld_stub_binder"
