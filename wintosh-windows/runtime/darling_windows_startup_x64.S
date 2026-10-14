/*
 * Wintosh-original source. Copyright (c) 2026 Tarek Wasfy.
 * SPDX-License-Identifier: MIT
 * See ../../LICENSE-WINTOSH-ORIGINAL-MIT.txt.
 */
/* Wintosh native x86_64 Darwin-start trampoline for Clang's integrated assembler. */

.intel_syntax noprefix
.text

.globl darling_windows_enter_darwin_start
.p2align 4
darling_windows_enter_darwin_start:
    mov rax, rcx
    mov rsp, rdx
    xor rbp, rbp
    jmp rax

.globl darling_windows_startup_probe
.p2align 4
darling_windows_startup_probe:
    mov eax, dword ptr [rsp]
    cmp eax, 2
    jne startup_probe_fail
    mov rax, qword ptr [rsp+8]
    test rax, rax
    jz startup_probe_fail
    cmp byte ptr [rax], 102
    jne startup_probe_fail
    mov rax, qword ptr [rsp+32]
    test rax, rax
    jz startup_probe_fail
    cmp byte ptr [rax], 102
    jne startup_probe_fail
    xor ecx, ecx
    sub rsp, 0x28
    jmp ExitProcess
startup_probe_fail:
    mov ecx, 91
    sub rsp, 0x28
    jmp ExitProcess

.section .drectve,"dr"
.ascii " -export:darling_windows_enter_darwin_start"
.ascii " -export:darling_windows_startup_probe"
