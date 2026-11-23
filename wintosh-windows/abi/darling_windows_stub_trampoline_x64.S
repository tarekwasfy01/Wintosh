/*
 * Wintosh-original x86_64 Darwin stub-trampoline probe.
 * SPDX-License-Identifier: MIT
 * See ../../LICENSE-WINTOSH-ORIGINAL-MIT.txt.
 *
 * The stack metadata offsets match the documented Darwin dyld binder frame:
 * rbp+8 = image-loader cache, rbp+16 = lazy-binding-info, rbp+24 = return.
 * The exported binder is installed as the trampoline for the native
 * Clang/System-V runner. It remains fail-closed when the lazy import cannot
 * be matched to exactly one active image session.
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

    /* Preserve the full OS-enabled xstate, including AVX/AVX-512 when
       Windows exposes it. Keep an FXSAVE fallback for older x86_64 hosts. */
    mov eax, 1
    cpuid
    bt ecx, 26                  /* XSAVE */
    jnc darling_windows_stub_save_legacy_state
    bt ecx, 27                  /* OSXSAVE */
    jnc darling_windows_stub_save_legacy_state
    xor ecx, ecx
    xgetbv
    mov [rbp-0x10], eax
    mov [rbp-0x18], edx
    mov eax, 0x0D
    xor ecx, ecx
    cpuid
    mov eax, ebx                /* bytes required for enabled XCR0 state */
    add eax, 63
    and eax, -64
    sub rsp, rax
    and rsp, -64
    mov qword ptr [rbp-0x20], 1
    mov eax, [rbp-0x10]
    mov edx, [rbp-0x18]
    /* XSAVE requires a clean, 64-byte header at offset 512. */
    mov qword ptr [rsp+0x200], 0
    mov qword ptr [rsp+0x208], 0
    mov qword ptr [rsp+0x210], 0
    mov qword ptr [rsp+0x218], 0
    mov qword ptr [rsp+0x220], 0
    mov qword ptr [rsp+0x228], 0
    mov qword ptr [rsp+0x230], 0
    mov qword ptr [rsp+0x238], 0
    xsave64 [rsp]
    jmp darling_windows_stub_state_saved

darling_windows_stub_save_legacy_state:
    sub rsp, 0x23F
    and rsp, -64
    mov qword ptr [rbp-0x20], 0
    fxsave64 [rsp]

darling_windows_stub_state_saved:

    mov rdi, [rbp+8]
    mov rsi, [rbp+16]
    call darling_windows_darwin_stub_probe_resolver
    mov r11, rax
    test r11, r11
    jz darling_windows_abi_dyld_stub_binder_fail_closed

    cmp qword ptr [rbp-0x20], 1
    jne darling_windows_stub_restore_legacy_state
    mov eax, [rbp-0x10]
    mov edx, [rbp-0x18]
    xrstor64 [rsp]
    jmp darling_windows_stub_state_restored

darling_windows_stub_restore_legacy_state:
    fxrstor64 [rsp]

darling_windows_stub_state_restored:
    lea rsp, [rbp-0x100]

    mov rax, [rsp+0x00]
    mov rbx, [rsp+0x08]
    mov rcx, [rsp+0x10]
    mov rdx, [rsp+0x18]
    mov rsi, [rsp+0x20]
    mov rdi, [rsp+0x28]
    mov r8, [rsp+0x30]
    mov r9, [rsp+0x38]
    mov r10, [rsp+0x40]

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
    jmp darling_windows_abi_dyld_stub_binder

.section .drectve,"dr"
.ascii " -export:darling_windows_darwin_stub_trampoline_probe"
.ascii " -export:darling_windows_darwin_stub_trampoline_fixture"
.ascii " -export:darling_windows_abi_dyld_stub_binder"
