.code

EXTERN cxx_vmexit_handler : PROC

asm_vmexit_entry PROC
        push    rax
        push    rcx
        push    rdx
        push    rbx
        push    rbp
        push    rbp
        push    rsi
        push    rdi
        push    r8
        push    r9
        push    r10
        push    r11
        push    r12
        push    r13
        push    r14
        push    r15

        mov     rcx, rsp
        sub     rsp, 20h
        call    cxx_vmexit_handler
        add     rsp, 20h

        ; eax = 0 normal (vmresume) ; eax = 1 unload (devirt return)
        test    eax, eax
        jnz     asm_devirt_path

        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     r11
        pop     r10
        pop     r9
        pop     r8
        pop     rdi
        pop     rsi
        pop     rbp
        pop     rbp
        pop     rbx
        pop     rdx
        pop     rcx
        pop     rax

        vmresume
asm_resume_hang:
        cli
        hlt
        jmp     asm_resume_hang

        ; devirt path.  handle_vmcall(unload) already issued VMXOFF and
        ; cleared CR4.VMXE, and stashed into volatile GPR slots:
        ;   r->rax = guest RIP (post-vmcall)
        ;   r->r8  = guest RSP
        ;   r->rcx = guest RFLAGS
        ;   r->rdx = guest CR3
        ; preserved GPRs (rbx, rbp, rsi, rdi, r12-r15) are the guest's.
        ; we restore everything, then switch CR3/RSP/flags and jmp.
asm_devirt_path:
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     r11
        pop     r10
        pop     r9
        pop     r8
        pop     rdi
        pop     rsi
        pop     rbp
        pop     rbp
        pop     rbx
        pop     rdx
        pop     rcx
        pop     rax

        mov     cr3, rdx
        mov     rsp, r8
        push    rcx
        popfq
        jmp     rax
asm_vmexit_entry ENDP
PUBLIC asm_vmexit_entry

; CONTEXT offsets: Rip=0F8h, Rsp=98h, EFlags=44h
k_guest_rsp    EQU 681Ch
k_guest_rip    EQU 681Eh
k_guest_rflags EQU 6820h

asm_capture_and_launch PROC
        mov     rax, [ rcx + 0F8h ]
        mov     r8,  k_guest_rip
        vmwrite r8, rax

        mov     rax, [ rcx + 98h ]
        mov     r8,  k_guest_rsp
        vmwrite r8, rax

        mov     eax, [ rcx + 44h ]
        mov     r8,  k_guest_rflags
        vmwrite r8, rax

        vmlaunch

        jc      launch_fail_invalid
        jz      launch_fail_valid
        xor     eax, eax
        ret
launch_fail_invalid:
        mov     eax, 1
        ret
launch_fail_valid:
        mov     eax, 2
        ret
asm_capture_and_launch ENDP
PUBLIC asm_capture_and_launch

asm_read_cs PROC
        mov     ax, cs
        movzx   eax, ax
        ret
asm_read_cs ENDP
PUBLIC asm_read_cs

asm_read_ss PROC
        mov     ax, ss
        movzx   eax, ax
        ret
asm_read_ss ENDP
PUBLIC asm_read_ss

asm_read_ds PROC
        mov     ax, ds
        movzx   eax, ax
        ret
asm_read_ds ENDP
PUBLIC asm_read_ds

asm_read_es PROC
        mov     ax, es
        movzx   eax, ax
        ret
asm_read_es ENDP
PUBLIC asm_read_es

asm_read_fs PROC
        mov     ax, fs
        movzx   eax, ax
        ret
asm_read_fs ENDP
PUBLIC asm_read_fs

asm_read_gs PROC
        mov     ax, gs
        movzx   eax, ax
        ret
asm_read_gs ENDP
PUBLIC asm_read_gs

asm_str PROC
        str     ax
        movzx   eax, ax
        ret
asm_str ENDP
PUBLIC asm_str

asm_sldt PROC
        sldt    ax
        movzx   eax, ax
        ret
asm_sldt ENDP
PUBLIC asm_sldt

asm_sgdt PROC
        sgdt    [ rcx ]
        ret
asm_sgdt ENDP
PUBLIC asm_sgdt

asm_sidt PROC
        sidt    [ rcx ]
        ret
asm_sidt ENDP
PUBLIC asm_sidt

asm_lar PROC
        lar     rax, rcx
        ret
asm_lar ENDP
PUBLIC asm_lar

asm_lsl PROC
        lsl     rax, rcx
        ret
asm_lsl ENDP
PUBLIC asm_lsl

asm_vmcall PROC
        vmcall
        ret
asm_vmcall ENDP
PUBLIC asm_vmcall

; unsigned __int64 asm_invept( unsigned __int64 type, void* descriptor )
; type in rcx, descriptor pointer in rdx.  returns 0 ok, 1 fail-invalid, 2 fail-valid.
asm_invept PROC
        invept  rcx, oword ptr [ rdx ]
        jc      invept_fail_invalid
        jz      invept_fail_valid
        xor     eax, eax
        ret
invept_fail_invalid:
        mov     eax, 1
        ret
invept_fail_valid:
        mov     eax, 2
        ret
asm_invept ENDP
PUBLIC asm_invept
END