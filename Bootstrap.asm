; Position-independent x64 thread entry. Resolve LoadLibraryW from initialized
; target-process loader tables, without assuming system DLLs share base addresses.
; RCX is a pointer to the UTF-16 DLL path in the target process.
PUBLIC LoadModBootstrap
PUBLIC LoadModBootstrapEnd
.code
LoadModBootstrap PROC
    push rbx
    push rbp
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 28h
    mov r15, rcx
    mov rax, gs:[60h]
    mov rax, [rax+18h]
    lea r14, [rax+20h]
    mov r13, [r14]
next_module:
    cmp r13, r14
    je not_found
    mov rbx, [r13+20h]
    test rbx, rbx
    jz advance_module
    mov eax, [rbx+3Ch]
    lea rax, [rbx+rax]
    mov r12d, [rax+88h]
    mov ebp, [rax+8Ch]
    test r12d, r12d
    jz advance_module
    lea rsi, [rbx+r12]
    mov edi, [rsi+18h]
    mov edx, [rsi+20h]
    add rdx, rbx
    xor ecx, ecx
next_name:
    cmp ecx, edi
    jae advance_module
    mov r8d, [rdx+rcx*4]
    add r8, rbx
    mov rax, 7262694C64616F4Ch
    cmp qword ptr [r8], rax
    jne advance_name
    cmp dword ptr [r8+8], 57797261h
    jne advance_name
    cmp byte ptr [r8+12], 0
    jne advance_name
    mov eax, [rsi+24h]
    add rax, rbx
    movzx eax, word ptr [rax+rcx*2]
    mov r8d, [rsi+1Ch]
    add r8, rbx
    mov eax, [r8+rax*4]
    ; An RVA inside the export directory is a forwarder string, not code.
    cmp eax, r12d
    jb invoke_loader
    mov r8d, r12d
    add r8d, ebp
    cmp eax, r8d
    jb advance_module
invoke_loader:
    add rax, rbx
    mov rcx, r15
    call rax
    jmp finished
advance_name:
    inc ecx
    jmp next_name
advance_module:
    mov r13, [r13]
    jmp next_module
not_found:
    xor eax, eax
finished:
    add rsp, 28h
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    ret
LoadModBootstrap ENDP
LoadModBootstrapEnd LABEL BYTE
END
