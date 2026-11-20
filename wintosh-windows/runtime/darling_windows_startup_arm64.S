/*
 * Wintosh-original source. Copyright (c) 2026 Tarek Wasfy.
 * SPDX-License-Identifier: MIT
 * See ../../LICENSE-WINTOSH-Original-MIT.txt.
 */
/* Native ARM64 Darwin-start transfer for Windows ARM64 Clang builds. */

.text
.p2align 2
.globl darling_windows_enter_darwin_start
darling_windows_enter_darwin_start:
    mov sp, x1
    mov x29, xzr
    mov x30, xzr
    br x0
