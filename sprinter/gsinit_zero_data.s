; =========================================================================
;  gsinit_zero_data.s — zero _DATA under the win0 layout (crt0_win0.s)
; =========================================================================
;  crt0_win0.s's own gsinit only copies _INITIALIZER -> _INITIALIZED and
;  clears _BSS; it does NOT zero _DATA. SDCC 4.5's z80 backend puts every
;  plain uninitialized global into _DATA, not _BSS (confirmed in this
;  project's own .map files: l__BSS is always 0, l__DATA carries the whole
;  state -- connection flags, the UNET loader's globals, the receive ring
;  buffer, ...). crt0_flat.s (the old flat WIN1+WIN2 crt0) zeroed _DATA
;  itself; this module supplies the identical fragment as a standalone
;  _GSINIT contribution so it can be linked right after crt0_win0.rel
;  without forking the SDK's own crt0.
;
;  _GSINIT areas concatenate across every linked .rel file, in link order,
;  as one straight-line routine (no per-fragment ret) ending at whichever
;  module supplies `.area _GSFINAL` + `ret` (crt0_win0.rel). This file must
;  be linked immediately after crt0_win0.rel for that concatenation to run
;  before _main -- see the win0 payload .lk script in the Makefile.
; =========================================================================

        .module gsinit_zero_data
        .globl  l__DATA
        .globl  s__DATA

        .area   _GSINIT
        ld      bc, #l__DATA
        ld      a, b
        or      a, c
        jr      z, gzd_done
        ld      hl, #s__DATA
        ld      (hl), #0
        dec     bc
        ld      a, b
        or      a, c
        jr      z, gzd_done
        ld      d, h
        ld      e, l
        inc     de
        ldir
gzd_done:
