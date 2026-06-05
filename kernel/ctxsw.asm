; ctxsw.asm -- Process context switch routines for Zenbite
;
; Provides ctx_switch() for cooperative multitasking between processes.
; Each process has its own stack; ctx_switch saves the current process's
; registers + stack pointer and restores the target process's state.

BITS 32
SECTION .text

; void ctx_switch(u32 *save_sp, u32 load_sp)
;   save_sp: pointer to where to store current ESP
;   load_sp: the ESP value to load (points to a saved frame)
;
; Saves all general-purpose registers + EFLAGS, switches stack,
; restores registers + EFLAGS, returns to the new process.
global ctx_switch
ctx_switch:
    cli                     ; no interrupts during the switch
    pushfd                  ; save EFLAGS
    pusha                   ; save EAX ECX EDX EBX ESP EBP ESI EDI
    ; Stack after pusha + pushfd:
    ;   [esp +  0] = EAX
    ;   [esp +  4] = ECX
    ;   [esp +  8] = EDX
    ;   [esp + 12] = EBX
    ;   [esp + 16] = ESP (original, discarded by popa)
    ;   [esp + 20] = EBP
    ;   [esp + 24] = ESI
    ;   [esp + 28] = EDI
    ;   [esp + 32] = EFLAGS
    ;   [esp + 36] = return address
    ;   [esp + 40] = save_sp (arg 1)
    ;   [esp + 44] = load_sp (arg 2)
    mov     eax, [esp + 40]     ; eax = save_sp
    mov     [eax], esp          ; *save_sp = current ESP
    mov     esp, [esp + 44]     ; switch to target stack
    popa                        ; restore registers
    popfd                       ; restore EFLAGS (re-enables interrupts)
    ret                         ; return to target process
