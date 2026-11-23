; Wintosh-original source. Copyright (c) 2026 Tarek Wasfy.
; SPDX-License-Identifier: MIT
; See ../../LICENSE-WINTOSH-ORIGINAL-MIT.txt.
;
; Transfer from the Win32 cdecl runner into an i386 Darwin process entry.
; The initial stack already contains argc, argv, envp and apple parameters.
.386
.model flat
.code

PUBLIC _darling_windows_enter_darwin_start
_darling_windows_enter_darwin_start PROC
	mov eax, DWORD PTR [esp + 4]
	mov edx, DWORD PTR [esp + 8]
	mov esp, edx
	jmp eax
_darling_windows_enter_darwin_start ENDP

END
