; Wintosh-original source. Copyright (c) 2026 Tarek Wasfy.
; SPDX-License-Identifier: MIT
; See ../../LICENSE-WINTOSH-ORIGINAL-MIT.txt.
;
; Transfer from the Win32 cdecl runner into an i386 Darwin process entry.
; The initial stack already contains argc, argv, envp and apple parameters.
.386
.model flat
.code
EXTERN _darling_windows_abi_tlv_get_addr:PROC

PUBLIC _darling_windows_enter_darwin_start
_darling_windows_enter_darwin_start PROC
	mov eax, DWORD PTR [esp + 4]
	mov edx, DWORD PTR [esp + 8]
	mov esp, edx
	jmp eax
_darling_windows_enter_darwin_start ENDP

; Darwin i386 TLV calls pass the descriptor in EAX, unlike the host C ABI.
; Adapt that register argument to the stack argument expected by C++.
PUBLIC _darling_windows_tlv_get_addr_i386
_darling_windows_tlv_get_addr_i386 PROC
	push eax
	call _darling_windows_abi_tlv_get_addr
	add esp, 4
	ret
_darling_windows_tlv_get_addr_i386 ENDP

END
