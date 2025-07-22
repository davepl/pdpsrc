        .globl  _wait_for_panel
_wait_for_panel:
        mov     $156, r0        / syscall number
        kcall                   / trap to kernel
        rts     pc              / return result in r0
