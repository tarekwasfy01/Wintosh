/*
 * Wintosh-original source. Copyright (c) 2026 Tarek Wasfy.
 * SPDX-License-Identifier: MIT
 * See ../../LICENSE-WINTOSH-Original-MIT.txt.
 */
/* Pass Apple's ARM64 variadic stack area to a typed dispatcher and restore
 * guest return registers. Apple's arguments begin at SP; Windows would put
 * the first variadic values in x2-x7. The C++ reader consumes only the slots
 * required by the method encoding.
 */

.text

.macro WINTOSH_ARM64_OBJC_ENTRY symbol, kind
.p2align 2
.globl \symbol
.seh_proc \symbol
\symbol:
    stp x29, x30, [sp, #-64]!
    .seh_save_fplr_x 64
    .seh_endprologue
    add x2, sp, #64
    add x3, sp, #16
    mov w4, #\kind
    mov x5, x8
    bl darling_windows_objc_arm64_message_dispatch
    ldr x0, [sp, #16]
    ldr x1, [sp, #24]
    ldr d0, [sp, #32]
    ldr d1, [sp, #40]
    ldr d2, [sp, #48]
    ldr d3, [sp, #56]
    .seh_startepilogue
    ldp x29, x30, [sp], #64
    .seh_save_fplr_x 64
    .seh_endepilogue
    ret
.seh_endproc
.endm

WINTOSH_ARM64_OBJC_ENTRY darling_windows_abi_objc_msgSend_arm64, 0
WINTOSH_ARM64_OBJC_ENTRY darling_windows_abi_objc_msgSendSuper_arm64, 1
WINTOSH_ARM64_OBJC_ENTRY darling_windows_abi_objc_msgSendSuper2_arm64, 2
