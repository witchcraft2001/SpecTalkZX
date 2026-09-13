; unetcall.s — UNET call trampoline + raw WIN1 page save/restore.
;
; MUST be linked into _HIGH — the private WIN2 page, which the win0 layout
; never repages — and never into WIN0/WIN1, the windows the code and the DLL
; share. Every UNET function call swaps WIN1 to the DLL's page, calls through
; the DLL's export table, then swaps WIN1 back to whatever was there before,
; exactly as libman's own _L_CALL does (raw port IN/OUT for the restore, since
; there is no DSS "query current window" call and the previous WIN1 page is a
; physical page number, not a DSS block we could re-map through dss_setwin).
;
; void unet_raw_call(u16 target, unet_regs *r);
;   struct unet_regs { u8 a; u16 de; u16 ix; u16 iy; };  (offsets 0,1,3,5)
;
; SDCC 4.5 default calling convention (sdcccall(1)): 1st u16 arg in HL, 2nd u16
; arg in DE — both already in registers, no stack-frame walk needed (unlike the
; SDCC 2.9 stack-based convention this trampoline was adapted from).
;
; WIN1 itself is switched by unetcore.s's unet_call with a raw OUT (#0xA2) of
; the DLL page's cached PHYSICAL number, paired with a _wrt_p1 update -- see
; that function's own comment for why (the win0 RST trampolines restore
; WIN1 = _wrt_p1 on every DSS/BIOS call, which would otherwise undo a plain
; dss_setwin(1, ...) immediately).
;
; PORT_WIN1 = 0xA2 (ports.h). Loads A/DE/IX/IY from *r, calls the DLL function at
; `target`, then stores the returned A/DE/IX/IY back into *r. HL/BC are scratch
; (the libman convention consumes them); IX/IY are saved for the C caller.

        .module unetcall
        .globl  _unet_raw_call
        .globl  _unet_win3_save
        .globl  _unet_win3_restore

        .area   _DATA
ut_target:  .ds 2
ut_rptr:    .ds 2
ut_iy:      .ds 2
ut_a:       .ds 1

        .area   _HIGH

; WIN3 (port 0xE2) save/restore, used while staging (unet_load): the DLL page
; is decoded/relocated there before it is ever swapped into WIN1. Same raw-port
; discipline as isauart.c's isa_open/isa_close. unet_call open-codes the same
; two instructions around each dispatch, since a DLL may map its own ISA card
; into WIN3 and is not required to put ours back.
_unet_win3_save::
        in      a, (#0xE2)
        ret

_unet_win3_restore::
        out     (#0xE2), a
        ret

_unet_raw_call::
        push    ix
        push    iy
        ld      (ut_target), hl   ; arg1 (HL) = target address
        ld      (ut_rptr), de     ; arg2 (DE) = r pointer
        ex      de, hl            ; HL = r pointer (DE now holds the old target; unused)
        ld      a, (hl)           ; +0 a
        ld      (ut_a), a
        inc     hl
        ld      e, (hl)           ; +1 de.l
        inc     hl
        ld      d, (hl)           ; +2 de.h
        inc     hl
        ld      c, (hl)           ; +3 ix.l
        inc     hl
        ld      b, (hl)           ; +4 ix.h
        inc     hl
        ld      a, (hl)           ; +5 iy.l
        ld      (ut_iy), a
        inc     hl
        ld      a, (hl)           ; +6 iy.h
        ld      (ut_iy + 1), a
        push    bc
        pop     ix                ; IX = input ix
        ld      iy, (ut_iy)       ; IY = input iy
        ld      a, (ut_a)         ; A  = input a  (DE already loaded above)
        ld      hl, (ut_target)
        call    unetcall_jphl     ; call the DLL function; returns here
        ; capture outputs before they are clobbered
        ld      (ut_a), a         ; A
        push    ix
        pop     bc                ; BC = returned ix
        push    iy
        pop     hl
        ld      (ut_iy), hl       ; returned iy
        ; store into *r (DE still holds the returned de)
        ld      hl, (ut_rptr)
        ld      a, (ut_a)
        ld      (hl), a           ; +0 a
        inc     hl
        ld      (hl), e           ; +1 de.l
        inc     hl
        ld      (hl), d           ; +2 de.h
        inc     hl
        ld      (hl), c           ; +3 ix.l
        inc     hl
        ld      (hl), b           ; +4 ix.h
        inc     hl
        ld      a, (ut_iy)
        ld      (hl), a           ; +5 iy.l
        inc     hl
        ld      a, (ut_iy + 1)
        ld      (hl), a           ; +6 iy.h
        pop     iy
        pop     ix
        ret

unetcall_jphl:
        jp      (hl)
