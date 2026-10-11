; Wintosh native x86_64 Darwin-start trampoline.
; RCX = Darwin entry address, RDX = prepared Darwin stack pointer.
; The Darwin entrypoint owns termination and is not expected to return.

OPTION CASEMAP:NONE
PUBLIC darling_windows_enter_darwin_start
PUBLIC darling_windows_startup_probe
EXTERN ExitProcess:PROC

.code
darling_windows_enter_darwin_start PROC
    mov rax, rcx
    mov rsp, rdx
    xor rbp, rbp
    jmp rax
darling_windows_enter_darwin_start ENDP

darling_windows_startup_probe PROC
    mov eax, dword ptr [rsp]
    cmp eax, 2
    jne startup_probe_fail
    xor ecx, ecx
    sub rsp, 28h
    jmp ExitProcess
startup_probe_fail:
    mov ecx, 91
    sub rsp, 28h
    jmp ExitProcess
darling_windows_startup_probe ENDP
END
