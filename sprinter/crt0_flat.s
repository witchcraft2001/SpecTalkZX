; =========================================================================
;  crt0_flat.s — SpecTalk Sprinter port: flat 32K layout (code spans WIN1+WIN2)
; =========================================================================
;  Used with CODE_LOC=0x4100, STACK=0xBFFF, and a loaded image PADDED to reach
;  WIN2 (>=16K) so DSS maps both WIN1 and WIN2 as the program's pages. Code grows
;  upward from 0x4100; data+stack sit high in WIN2. Unlike the default crt0 this
;  zeroes _DATA (our globals live there, not _BSS); unlike crt0_page2 it does NOT
;  GETMEM (WIN2 is already owned via the 2-page image) and keeps the header SP.
; =========================================================================

        .module crt0_flat
        .globl  _main
        .globl  l__INITIALIZER
        .globl  s__INITIALIZED
        .globl  s__INITIALIZER
        .globl  l__DATA
        .globl  s__DATA
        .globl  l__BSS
        .globl  s__BSS

        .area   _CODE

_entry::
        ; cmdline ptr is in IX; save it AFTER gsinit (gsinit zeroes _DATA).
        push    ix
        call    gsinit
        pop     hl
        ld      (__cmdline), hl

        call    _main

        ; main() returned - SDCC 4.x return value in DE.
        ld      b, e
        ld      c, #0x41        ; DSS.Exit
        rst     #0x10

        ; ----- Area ordering (must match SDCC's) -----
        .area   _HOME
        .area   _INITIALIZER
        .area   _GSINIT
        .area   _GSFINAL

        .area   _DATA
        .area   _INITIALIZED
        .area   _BSEG
        .area   _BSS
        .area   _HEAP

        ; ----- Global initialization -----
        .area   _GSINIT
gsinit::
        ; Zero _DATA (SDCC puts our globals here, not _BSS)
        ld      bc, #l__DATA
        ld      a, b
        or      a, c
        jr      z, gsf_copy
        ld      hl, #s__DATA
        ld      (hl), #0
        dec     bc
        ld      a, b
        or      a, c
        jr      z, gsf_copy
        ld      d, h
        ld      e, l
        inc     de
        ldir

gsf_copy:
        ; Copy INITIALIZER -> INITIALIZED
        ld      bc, #l__INITIALIZER
        ld      a, b
        or      a, c
        jr      z, gsf_bss
        ld      de, #s__INITIALIZED
        ld      hl, #s__INITIALIZER
        ldir

gsf_bss:
        ; Clear _BSS (empty in our build, but keep it correct)
        ld      hl, #s__BSS
        ld      bc, #l__BSS
        ld      a, b
        or      a, c
        jr      z, gsf_done
        ld      (hl), #0
        dec     bc
        ld      a, b
        or      a, c
        jr      z, gsf_done
        ld      d, h
        ld      e, l
        inc     de
        ldir

gsf_done:
        ; SDCC's per-variable GSINIT init code follows here, then _GSFINAL ret.
        .area   _GSFINAL
        ret

        .area   _DATA
__cmdline::
        .ds     2
