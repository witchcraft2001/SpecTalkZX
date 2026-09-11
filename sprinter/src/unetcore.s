; unetcore.s - UNET L1 DLL loader + per-call windowed dispatch, hand-written
; Z80 asm replacing unet.c. Same algorithm as that C file (see its own header
; comment, preserved below for the record), transliterated to sdasz80 and
; SDCC 4.5's sdcccall(1) C linkage so net_unet.c and unetldcore.s call these
; exactly as they called the C version.
;
; The relocation loop (ul_remake/ul_reloc) reuses the exact bit-shift
; technique from .../sources/unet_libs/unet_libs_asm/extern/libman/libman/
; libman_core13.asm's `remake`/`reloc` (rla through carry, one relocation-
; bitmap byte at a time, conditionally adding the page-high byte to each
; flagged code byte) -- that part of this port IS a direct, deliberate copy
; of libman's own proven instruction sequence (only the entry setup differs:
; our "c" -- the byte added on a relocation -- is the constant WIN1_PAGE_HI,
; not a caller-supplied value, and our stop condition additionally guards
; code_size <= 32 up front). The L1 header/RLE-decode/INIT/GETCAPS
; sequencing is this port's own (unet.c's, transliterated): it streams
; through dss_read in chunks rather than libman's raw low-level disk I/O
; with an accelerator-driven bounce buffer, because this port already has a
; buffered dss_read and doesn't need to reimplement disk I/O by hand.
;
; Design decisions carried over from unet.c's own header comment:
;   - The DLL's page is mapped into WIN1 only for the span of each unet_call
;     (a raw OUT of the cached physical page paired with a _wrt_p1 update,
;     then unet_raw_call, then the reverse -- see unet_call's own comment)
;     instead of once at load and left resident -- SprinTalk's own code
;     occupies WIN1 the rest of the time. WIN3 is saved and restored across
;     the same span, since the DLL maps its own ISA card there.
;   - Staging (header parse, RLE decode, relocation) happens through WIN3
;     with the target page already allocated; the relocation-bitmap/read-
;     chunk scratch lives in _DATA (WIN2, uc_chunk/uc_bitmap below) rather
;     than a borrowed WIN1 page -- under the win0 layout WIN1 is not a safe
;     place to park scratch state (see unet_call's own comment).
;   - The DSS file handle stays open across INIT (function 0) and is closed
;     only after it returns, per the libman contract: INIT receives the open
;     handle in A and may read the DLL's own trailing payload from it
;     (UNETRTL.DLL/UNET509B.DLL both carry one; UNETESP.DLL does not).
;   - code_size > 0x4000 is the window-overrun guard.
;   - Every wrapper checks unet_loaded() first.
;   - GETCAPS's ABI word (IX) is validated (major byte + CAP_TCP).
;
; sdcccall(1) conventions relied on below (verified against SDCC 4.5's own
; codegen, both caller- and callee-side, for each exact signature used here):
;   1-arg pointer: HL.                    1-arg u8: A.
;   (u8, ptr) or (ptr, ptr) or (ptr, u16): slot1 as above, slot2 in DE.
;   (ptr, u16, u16, ptr) [unet_recv]:      slot1 HL, slot2 DE, slot3+ pushed
;                                          right-to-left, 2 bytes each, so at
;                                          callee entry (before any of our
;                                          own pushes) slot3 is at SP+2,
;                                          slot4 at SP+4.
;   (u8, ptr, u16) [unet_getinfo]:         slot1 A, slot2 DE, slot3 SP+2.
;   (u8, u8) [dss_setwin only]:            slot1 A, slot2 L.
;   (ptr, u8) [dss_open only]:             slot1 HL, slot2 pushed as ONE byte.
;   return: A for u8/i8, DE for u16/i16/pointers.
; dss_open/dss_read/dss_seek clean up their OWN pushed stack arguments before
; returning, so callers here never pop after them.

        .module unetcore

        .globl _unet_load
        .globl _unet_free
        .globl _unet_loaded
        .globl _unet_dll_name
        .globl _unet_caps
        .globl _unet_abi
        .globl _unet_call
        .globl _unet_connect
        .globl _unet_send
        .globl _unet_recv
        .globl _unet_close
        .globl _unet_netinit
        .globl _unet_netdone
        .globl _unet_netstatus
        .globl _unet_getinfo
        .globl _unet_lasterr

        .globl _unet_raw_call
        .globl _unet_win3_save
        .globl _unet_win3_restore

        .globl _dss_open
        .globl _dss_close
        .globl _dss_read
        .globl _dss_seek
        .globl _dss_getmem
        .globl _dss_freemem
        .globl _dss_setwin

        ; win0 runtime globals (lib/win0/win0_rt.s / crt0_win0.s) -- the
        ; caller's own WIN1 physical page, kept current so any RST trampoline
        ; that fires during a DLL call restores WIN1 to the DLL, not to us.
        .globl _wrt_p1

WIN1_PAGE_HI    = 0x40
UNET_RELOC_MAX  = 2048
UNET_CHUNK      = 128

FN_INIT         = 0
FN_FINI         = 1
FN_GETCAPS      = 2
FN_NETINIT      = 3
FN_NETDONE      = 4
FN_CONNECT      = 5
FN_SEND         = 6
FN_RECV         = 7
FN_CLOSE        = 8
FN_STATUS       = 9
FN_GETINFO      = 15
FN_LASTERR      = 16
NERR_OK         = 0
NERR_STATE      = 11
LOAD_OK         = 0
LOAD_ENOMEM     = -1
LOAD_EOPEN      = -2
LOAD_EFORMAT    = -3
LOAD_ETOOBIG    = -4
LOAD_EINIT      = -5
LOAD_EABI       = -6

; ==========================================================================
; State (zeroed at start by gsinit -- crt0_win0.s via the project's
; gsinit_zero_data.s fragment; see that file's header).
; ==========================================================================
        .area _DATA
g_block:        .ds 1           ; DSS memory BLOCK ID (1..255), not a page number
g_phys:         .ds 1           ; physical page of g_block's first page, for raw OUTs
g_caps:         .ds 2
g_abi:          .ds 2
g_name:         .ds 17
g_path:         .ds 2
g_fd:           .ds 1
g_win3saved:    .ds 1
g_filesize:     .ds 2
g_codesize:     .ds 2
g_relocsize:    .ds 2
g_compressed:   .ds 1
g_chunklen:     .ds 2
g_chunkpos:     .ds 2
g_fileleft:     .ds 2
g_expected:     .ds 2
g_op:           .ds 2
g_cnt:          .ds 2
g_reloclen:     .ds 2
g_regs:         .ds 7           ; scratch unet_regs, shared by every wrapper below
uc_hdr:         .ds 8           ; L1 header scratch (8 bytes read up front)
uc_target:      .ds 2
uc_fntmp:       .ds 1
uc_rptr:        .ds 2
uc_callsave:    .ds 1
uc_win3save:    .ds 1
uc_flagsptr:    .ds 2
uc_bitsptr:     .ds 2

; Read-chunk buffer + relocation bitmap for unet_load's decode/relocate pass.
; Formerly a page borrowed into WIN1 (SCRATCH_BASE/SCRATCH_BITMAP) -- moved
; here because under the win0 layout every dss_read mid-decode would have its
; RST #10 trampoline restore WIN1 to the caller's own page (win0_rt.s), which
; would evict both this buffer and unet_load's own code if it happened to
; link into WIN1. _DATA (WIN2) is never touched by that restore.
uc_chunk:       .ds 128
uc_bitmap:      .ds 2048

        .area _CODE

; ==========================================================================
; unet_load(const char *path) -- path in HL.
; Out: A = LOAD_OK(0) or a negative LOAD_* code.
; ==========================================================================
_unet_load::
        ld      (g_path), hl
        ld      a, (g_block)
        or      a
        jr      z, ul_no_prev
        call    _unet_free
ul_no_prev:
        call    _dss_getmem
        cp      #0xFF                   ; the SDK wrapper's own failure value. DSS
        jr      nz, ul_getmem_ok        ; block IDs run 1..255 (BIOS EMM_FN2), so 0
        ld      a, #LOAD_ENOMEM         ; -- what gsinit leaves in g_block -- is a
        ret                             ; distinct, never-allocated "not loaded"
                                        ; sentinel, and must not be overwritten here
ul_getmem_ok:
        ld      (g_block), a
        call    _unet_win3_save
        ld      (g_win3saved), a
        ld      a, (g_block)
        ld      l, a
        ld      a, #3
        call    _dss_setwin             ; WIN3 -> g_block; target page visible at 0xC000
        in      a, (#0xE2)              ; ... and cache the PHYSICAL page behind it.
        ld      (g_phys), a             ; dss_getmem returns a block ID, and SETWIN
                                        ; (block, page-in-block) is the only thing
                                        ; that understands one -- the window ports
                                        ; take physical page numbers. Every later
                                        ; raw OUT (WIN3 below, WIN1 in unet_call)
                                        ; must therefore use g_phys, never g_block.
                                        ; Same read-back idiom as lib/win0/loader.c
                                        ; (dss_setwin(1,b) then inp(PORT_WIN1)) and
                                        ; libman's own _L_CALL (in a,(0A2h)).

        ; dss_open(g_path, O_RDONLY=0)
        xor     a
        ld      h, a
        push    hl
        inc     sp                      ; mode=0 pushed as one byte
        ld      hl, (g_path)
        call    _dss_open               ; -> DE = fd, or 0xFFFF on error
        ld      a, d
        add     a, a
        jr      c, ul_open_failed
        ld      a, e
        ld      (g_fd), a
        jr      ul_opened
ul_open_failed:
        ld      a, (g_win3saved)
        call    _unet_win3_restore
        ld      a, (g_block)
        call    _dss_freemem
        xor     a
        ld      (g_block), a
        ld      a, #LOAD_EOPEN
        ret

ul_opened:
        ; read the 8-byte header
        ld      hl, #8
        push    hl
        ld      a, (g_fd)
        ld      de, #uc_hdr
        call    _dss_read               ; -> DE = bytes read, or -1
        ld      a, d
        or      a
        jp      nz, ul_bad_format
        ld      a, e
        cp      #8
        jp      nz, ul_bad_format
        ld      a, (uc_hdr)
        cp      #'L'
        jp      nz, ul_bad_format
        ld      a, (uc_hdr + 1)
        cp      #'1'
        jp      nz, ul_bad_format

        ld      hl, (uc_hdr + 2)
        ld      (g_filesize), hl
        ld      hl, (uc_hdr + 4)
        ld      (g_codesize), hl
        ld      hl, (uc_hdr + 6)
        ld      (g_relocsize), hl

        ; file_size < 8 ?
        ld      hl, (g_filesize)
        ld      de, #8
        or      a
        sbc     hl, de
        jp      c, ul_bad_format
        ; code_size < 32 ?
        ld      hl, (g_codesize)
        ld      de, #32
        or      a
        sbc     hl, de
        jp      c, ul_bad_format
        ; code_size > 0x4000 ?
        ld      hl, (g_codesize)
        ld      de, #0x4000
        or      a
        sbc     hl, de
        jr      c, ul_cs_max_ok
        ld      a, h
        or      l
        jr      z, ul_cs_max_ok
        jp      ul_bad_format
ul_cs_max_ok:
        ; reloc_size > UNET_RELOC_MAX ?
        ld      hl, (g_relocsize)
        ld      de, #UNET_RELOC_MAX
        or      a
        sbc     hl, de
        jr      c, ul_rs_ok
        ld      a, h
        or      l
        jr      z, ul_rs_ok
        jp      ul_etoobig
ul_rs_ok:
        ; expected = code_size + reloc_size ; compressed = (expected != file_size)
        ld      hl, (g_codesize)
        ld      de, (g_relocsize)
        add     hl, de
        ld      (g_expected), hl
        ld      de, (g_filesize)
        or      a
        sbc     hl, de
        ld      a, h
        or      l
        jr      z, ul_not_compressed
        ld      a, #1
        jr      ul_set_compressed
ul_not_compressed:
        xor     a
ul_set_compressed:
        ld      (g_compressed), a

        ; dss_seek(fd, 0, SEEK_SET)
        ld      h, #0
        push    hl
        inc     sp                      ; origin = 0
        ld      hl, #0
        push    hl                      ; offset_hi = 0
        push    hl                      ; offset_lo = 0
        ld      a, (g_fd)
        call    _dss_seek

        xor     a
        ld      (g_chunklen), a
        ld      (g_chunklen + 1), a
        ld      (g_chunkpos), a
        ld      (g_chunkpos + 1), a
        ld      hl, (g_filesize)
        ld      (g_fileleft), hl

        call    ul_decode
        jp      c, ul_decode_failed

        call    ul_relocate

        ; cache the self-reported name (16 bytes at page+16 = 0xC010)
        ld      hl, #0xC010
        ld      de, #g_name
        ld      b, #16
ul_copyname:
        ld      a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        djnz    ul_copyname
        xor     a
        ld      (de), a

        ld      a, (g_win3saved)
        call    _unet_win3_restore     ; WIN3 must be clear before any WIN1 swap/DSS call

        ; INIT (function 0): dispatched with the DSS file handle still open
        ; in A -- closed only after it returns (see the file header).
        ld      a, (g_fd)
        ld      (g_regs), a
        xor     a
        ld      (g_regs + 1), a
        ld      (g_regs + 2), a
        ld      (g_regs + 3), a
        ld      (g_regs + 4), a
        ld      (g_regs + 5), a
        ld      (g_regs + 6), a
        ld      a, #FN_INIT
        ld      de, #g_regs
        call    _unet_call
        push    af
        ld      a, (g_fd)
        call    _dss_close
        pop     af
        cp      #NERR_OK
        jr      z, ul_init_ok
        ld      a, (g_block)
        call    _dss_freemem
        xor     a
        ld      (g_block), a
        ld      a, #LOAD_EINIT
        ret

ul_init_ok:
        ld      a, #FN_GETCAPS
        call    uc_call0
        ld      hl, (g_regs + 1)
        ld      (g_caps), hl
        ld      hl, (g_regs + 3)
        ld      (g_abi), hl
        ld      a, (g_abi + 1)
        cp      #1
        jr      nz, ul_abi_bad
        ld      hl, (g_caps)
        ld      a, l
        and     #1                      ; UNET_CAP_TCP
        jr      z, ul_abi_bad
        xor     a
        ret                              ; LOAD_OK
ul_abi_bad:
        call    _unet_free
        ld      a, #LOAD_EABI
        ret

ul_decode_failed:
        ; falls through to ul_bad_format's cleanup (fd still open, WIN3 still
        ; mapped to the DLL's page -- exactly what that label expects)

ul_bad_format:
        ld      a, (g_fd)
        call    _dss_close
        ld      a, (g_win3saved)
        call    _unet_win3_restore
        ld      a, (g_block)
        call    _dss_freemem
        xor     a
        ld      (g_block), a
        ld      a, #LOAD_EFORMAT
        ret

ul_etoobig:
        ld      a, (g_fd)
        call    _dss_close
        ld      a, (g_win3saved)
        call    _unet_win3_restore
        ld      a, (g_block)
        call    _dss_freemem
        xor     a
        ld      (g_block), a
        ld      a, #LOAD_ETOOBIG
        ret

; ==========================================================================
; ul_next_byte: read one byte from the file stream, refilling the _DATA
; scratch chunk buffer (uc_chunk, UNET_CHUNK bytes) via dss_read as
; needed. Out: A = byte, CF=0; CF=1 on EOF/read error. Clobbers AF,BC,DE,HL.
; ==========================================================================
ul_next_byte:
        ld      hl, (g_chunkpos)
        ld      de, (g_chunklen)
        or      a
        sbc     hl, de
        jr      c, ul_nb_have           ; pos < len: a buffered byte is ready
        ld      hl, (g_fileleft)
        ld      a, h
        or      l
        jr      z, ul_nb_eof
        ld      de, #UNET_CHUNK
        or      a
        sbc     hl, de
        jr      c, ul_nb_want_left
        ld      hl, #UNET_CHUNK
        jr      ul_nb_want_ready
ul_nb_want_left:
        ld      hl, (g_fileleft)
ul_nb_want_ready:
        push    hl
        ld      a, (g_fd)
        ld      de, #uc_chunk
        call    _dss_read
        ld      a, d
        add     a, a
        jr      c, ul_nb_eof
        ld      a, d
        or      e
        jr      z, ul_nb_eof
        ld      (g_chunklen), de
        ld      a, (g_phys)
        call    _unet_win3_restore      ; dss_read may have repointed WIN3
        ld      hl, (g_fileleft)
        or      a
        sbc     hl, de
        ld      (g_fileleft), hl
        xor     a
        ld      (g_chunkpos), a
        ld      (g_chunkpos + 1), a
ul_nb_have:
        ld      hl, (g_chunkpos)
        ld      de, #uc_chunk
        add     hl, de
        ld      a, (hl)
        push    af
        ld      hl, (g_chunkpos)
        inc     hl
        ld      (g_chunkpos), hl
        pop     af
        or      a
        ret
ul_nb_eof:
        scf
        ret

; ==========================================================================
; ul_put_out: store A at output position g_op -- the target page (WIN3,
; 0xC000 + op) if op < code_size, else the relocation bitmap (_DATA,
; uc_bitmap + (op - code_size)). Does not touch g_op itself. Clobbers
; AF,HL,DE.
; ==========================================================================
; Note: none of the HL/DE arithmetic below touches A, so the byte to write
; needs no explicit save/restore -- an earlier version pushed/popped AF
; around the sbc comparison "to be safe", which instead POPPED BACK the
; stale pre-call flags and fed the jr z/jr c branch a comparison result
; from whatever the caller last did (ul_next_byte's "was the byte zero?"),
; not this function's own op-vs-code_size test. Every zero code byte could
; misroute here into the bitmap write, corrupting whatever _DATA precedes
; uc_bitmap once op is far enough below code_size to underflow past it.
ul_put_out:
        ld      hl, (g_codesize)
        ld      de, (g_op)
        or      a
        sbc     hl, de
        jr      z, ul_po_bitmap
        jr      c, ul_po_bitmap
        ld      hl, (g_op)
        ld      de, #0xC000
        add     hl, de
        ld      (hl), a
        ret
ul_po_bitmap:
        ld      hl, (g_op)
        ld      de, (g_codesize)
        or      a
        sbc     hl, de
        ld      de, #uc_bitmap
        add     hl, de
        ld      (hl), a
        ret

; ==========================================================================
; ul_decode: decode the L1 stream (zero-RLE if g_compressed, plain copy
; otherwise) into the target page + bitmap, per g_expected. g_op is reset to
; 0 here. Out: CF=0 ok, CF=1 malformed/short stream. Clobbers everything.
; ==========================================================================
ul_decode:
        ld      hl, #0
        ld      (g_op), hl
        ld      a, (g_compressed)
        or      a
        jr      nz, ud_prefix

ud_plain_loop:
        ld      hl, (g_op)
        ld      de, (g_expected)
        or      a
        sbc     hl, de
        jp      nc, ud_ok
        call    ul_next_byte
        jp      c, ud_fail
        call    ul_put_out
        ld      hl, (g_op)
        inc     hl
        ld      (g_op), hl
        jr      ud_plain_loop

ud_prefix:
        ld      hl, (g_op)
        ld      de, #16
        or      a
        sbc     hl, de
        jr      nc, ud_prefix_done
        ld      hl, (g_op)
        ld      de, (g_expected)
        or      a
        sbc     hl, de
        jr      nc, ud_prefix_done
        call    ul_next_byte
        jp      c, ud_fail
        call    ul_put_out
        ld      hl, (g_op)
        inc     hl
        ld      (g_op), hl
        jr      ud_prefix
ud_prefix_done:

ud_rle_loop:
        ld      hl, (g_op)
        ld      de, (g_expected)
        or      a
        sbc     hl, de
        jp      nc, ud_ok
        call    ul_next_byte
        jp      c, ud_fail
        or      a
        jr      z, ud_rle_run
        call    ul_put_out
        ld      hl, (g_op)
        inc     hl
        ld      (g_op), hl
        jr      ud_rle_loop
ud_rle_run:
        call    ul_next_byte
        jp      c, ud_fail
        or      a
        jr      nz, ud_cnt_set
        ld      hl, #256
        jr      ud_cnt_ready
ud_cnt_set:
        ld      l, a
        ld      h, #0
ud_cnt_ready:
        ld      (g_cnt), hl
ud_run_loop:
        ld      hl, (g_cnt)
        ld      a, h
        or      l
        jr      z, ud_rle_loop
        ld      hl, (g_op)
        ld      de, (g_expected)
        or      a
        sbc     hl, de
        jp      nc, ud_ok
        xor     a
        call    ul_put_out
        ld      hl, (g_op)
        inc     hl
        ld      (g_op), hl
        ld      hl, (g_cnt)
        dec     hl
        ld      (g_cnt), hl
        jr      ud_run_loop

ud_ok:
        or      a
        ret
ud_fail:
        scf
        ret

; ==========================================================================
; ul_relocate / ul_remake / ul_reloc: L1 relocation, ported directly from
; libman_core13.asm's remake/reloc (see this file's header). Adds
; WIN1_PAGE_HI to every code byte the bitmap flags, walking bit-by-bit via
; `rla` through carry (MSB first) instead of the i>>3/i&7 arithmetic the C
; original used.
; ==========================================================================
ul_relocate:
        ld      hl, (g_codesize)
        ld      de, #32
        or      a
        sbc     hl, de
        jr      nc, url_have_len
        ld      hl, #0
url_have_len:
        ld      a, h
        or      l
        ret     z                       ; nothing eligible for relocation
        ex      de, hl                  ; de = code length (bytes = bits) to walk
        ld      hl, #0xC020             ; page(0xC000) + 32: first relocatable byte
        ld      iy, #uc_bitmap
        ld      c, #WIN1_PAGE_HI
; Bit-walk technique ported verbatim from libman_core13.asm's remake/reloc:
; A holds the current bitmap byte across all 8 `rla` rotations (MSB first);
; the shadow AF' -- not the stack -- is what lets ul_reloc use A for its own
; work (reading/writing the code byte) without disturbing that rotation
; state, exactly as the reference does.
ul_remake:
        ld      b, #8
        ld      a, 0 (iy)
ul_rmk2:
        rla
        call    c, ul_reloc
        ex      af, af'
        inc     hl
        dec     de
        ld      a, d
        or      e
        ret     z
        ex      af, af'
        djnz    ul_rmk2
        inc     iy
        jr      ul_remake

ul_reloc:
        ex      af, af'
        ld      a, (hl)
        add     a, c
        ld      (hl), a
        ex      af, af'
        ret

        .area   _HIGH

; ==========================================================================
; unet_call(u8 fn, unet_regs *r) -- fn in A, r in DE. Out: A = status
; (NERR_STATE if nothing is loaded, also written into r->a in that case).
;
; MUST live in _HIGH (WIN2, never displaced by the WIN1 swap below): under
; the win0 layout _CODE spans WIN0+WIN1, and this function's own PC keeps
; running while WIN1 is repointed to the DLL, so its bytes can never
; themselves be reached through WIN1 (see the port plan's win0 notes).
;
; WIN1 is switched with a raw OUT, not dss_setwin: under win0 every RST #10
; call is intercepted by a trampoline (win0_rt.s) that restores WIN1 to
; _wrt_p1 on return, which would instantly undo dss_setwin(1, ...). _wrt_p1
; is repointed at the DLL's page for the span of the call, so a trampoline
; firing mid-call (e.g. a timer IM1, or the DLL itself calling a DSS
; function) restores WIN1 back to the DLL rather than evicting it.
; ==========================================================================
_unet_call::
        ld      (uc_fntmp), a
        ld      (uc_rptr), de
        ld      a, (g_block)
        or      a
        jr      nz, uc_loaded
        ld      hl, (uc_rptr)
        ld      a, #NERR_STATE
        ld      (hl), a
        ret
uc_loaded:
        ld      a, (uc_fntmp)
        add     a, a
        ld      c, a                    ; c = fn*2
        ld      a, (uc_fntmp)
        add     a, c                    ; a = fn*3
        ld      l, a
        ld      h, #0
        ld      de, #0x4020             ; WIN1_BASE + 0x20
        add     hl, de
        ld      (uc_target), hl

        in      a, (#0xE2)              ; WIN3 is the DLL's scratch: the UNET ABI
        ld      (uc_win3save), a        ; lets it map its ISA card there per call and
                                        ; does not promise to put ours back, so save
                                        ; and restore it around every dispatch.
        ld      a, (_wrt_p1)            ; caller's own WIN1 physical page
        ld      (uc_callsave), a
        ld      a, (g_phys)             ; the DLL's PHYSICAL page (g_block is a block
        ld      (_wrt_p1), a            ; ID -- see unet_load); published first so a
        out     (#0xA2), a              ; nested trampoline restores WIN1 to the DLL

        ld      hl, (uc_target)
        ld      de, (uc_rptr)
        call    _unet_raw_call

        ld      a, (uc_callsave)
        ld      (_wrt_p1), a
        out     (#0xA2), a              ; WIN1 = caller again
        ld      a, (uc_win3save)
        out     (#0xE2), a              ; WIN3 = caller's again

        ld      hl, (uc_rptr)
        ld      a, (hl)
        ret

        .area   _CODE

; uc_call0: A=fn. Zero g_regs' de/ix/iy fields, dispatch through unet_call.
uc_call0:
        push    af
        ld      hl, #g_regs + 1
        xor     a
        ld      (hl), a
        inc     hl
        ld      (hl), a
        inc     hl
        ld      (hl), a
        inc     hl
        ld      (hl), a
        inc     hl
        ld      (hl), a
        inc     hl
        ld      (hl), a
        pop     af
        ld      de, #g_regs
        jp      _unet_call

; uc_neg16: DE = -(i16)A (A is a small non-negative NERR_* code). Clobbers
; AF,HL,DE.
uc_neg16:
        ld      e, a
        ld      d, #0
        ld      hl, #0
        or      a
        sbc     hl, de
        ex      de, hl
        ret

; ==========================================================================
; unet_free(void)
; ==========================================================================
_unet_free::
        ld      a, (g_block)
        or      a
        ret     z
        ld      a, #FN_FINI
        call    uc_call0
        ld      a, (g_block)
        call    _dss_freemem
        xor     a
        ld      (g_block), a
        xor     a
        ld      (g_caps), a
        ld      (g_caps + 1), a
        ld      (g_abi), a
        ld      (g_abi + 1), a
        ld      (g_name), a
        ret

_unet_loaded::
        ld      a, (g_block)
        or      a
        jr      nz, uloaded_yes
        xor     a
        ret
uloaded_yes:
        ld      a, #1
        ret

_unet_dll_name::
        ld      de, #g_name
        ret
_unet_caps::
        ld      de, (g_caps)
        ret
_unet_abi::
        ld      de, (g_abi)
        ret

; ==========================================================================
; unet_connect(const char *host, const char *port) -- host in HL, port in DE.
; ==========================================================================
_unet_connect::
        ld      (g_regs + 3), de        ; ix field = port
        ld      (g_regs + 1), hl        ; de field = host
        xor     a
        ld      (g_regs), a             ; a field = channel 0
        ld      (g_regs + 5), a
        ld      (g_regs + 6), a
        call    _unet_loaded
        or      a
        jr      nz, uconn_go
        ld      a, #NERR_STATE
        neg
        ret
uconn_go:
        ld      a, #FN_CONNECT
        ld      de, #g_regs
        jp      _unet_call

; ==========================================================================
; unet_send(const void *buf, u16 len) -- buf in HL, len in DE.
; Out: DE = bytes sent (>=0), or -(i16)status on failure.
; ==========================================================================
_unet_send::
        ld      (g_regs + 3), de        ; ix field = len
        ld      (g_regs + 1), hl        ; de field = buf
        xor     a
        ld      (g_regs), a
        ld      (g_regs + 5), a
        ld      (g_regs + 6), a
        call    _unet_loaded
        or      a
        jr      nz, usend_go
        ld      a, #NERR_STATE
        jp      uc_neg16
usend_go:
        ld      a, #FN_SEND
        ld      de, #g_regs
        call    _unet_call
        cp      #NERR_OK
        jr      z, usend_ok
        jp      uc_neg16
usend_ok:
        ld      de, (g_regs + 1)
        ret

; ==========================================================================
; unet_recv(void *buf, u16 max, u16 timeout_ms, u16 *flags)
; buf in HL, max in DE, timeout_ms at entry SP+2, flags at entry SP+4.
; Out: DE = bytes received (>=0), or -(i16)status.
; ==========================================================================
_unet_recv::
        ld      (g_regs + 1), hl        ; de field = buf
        ld      (g_regs + 3), de        ; ix field = max
        ld      hl, #2
        add     hl, sp
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a
        ld      (g_regs + 5), hl        ; iy field = timeout_ms
        ld      hl, #4
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (uc_flagsptr), de
        xor     a
        ld      (g_regs), a
        call    _unet_loaded
        or      a
        jr      nz, urecv_go
        ld      de, (uc_flagsptr)
        ld      a, d
        or      e
        jr      z, urecv_err_noflags
        xor     a
        ld      (de), a
        inc     de
        ld      (de), a
urecv_err_noflags:
        ld      a, #NERR_STATE
        call    uc_neg16
        jr      urecv_return
urecv_go:
        ld      a, #FN_RECV
        ld      de, #g_regs
        call    _unet_call
        ld      hl, (uc_flagsptr)
        ld      a, h
        or      l
        jr      z, urecv_noflags2
        ld      de, (g_regs + 3)
        ld      (hl), e
        inc     hl
        ld      (hl), d
urecv_noflags2:
        ld      a, (g_regs)              ; status, refreshed by unet_raw_call's own output
        cp      #NERR_OK
        jr      z, urecv_ok
        call    uc_neg16
        jr      urecv_return
urecv_ok:
        ld      de, (g_regs + 1)
urecv_return:
        ; sdcccall(1) is callee-cleans-up for stack-passed arguments (slot 3+
        ; -- verified against SDCC's own codegen, see this file's header):
        ; discard the 4 bytes (timeout_ms, flags) pushed by our caller before
        ; returning, exactly like the SDK's own dss_open/dss_read/dss_seek do.
        pop     iy
        ld      hl, #4
        add     hl, sp
        ld      sp, hl
        jp      (iy)

; ==========================================================================
; unet_lasterr(char *dst, u16 max) -- dst in HL, max in DE. Out: A = status.
;
; Function 16: the backend's OWN post-mortem of the last failure, not ours.
; UNETRTL formats a fixed line -- ISA slot and I/O base, the stage that failed,
; its NERR_*, the internal TCP and resolver failure codes, and the four NIC
; transmit-diagnostic bytes (stage / ISR / TSR / CR) captured at the point of
; failure. TSR is the interesting one: it says whether the frame physically
; left the card, which is the difference between "the peer ignored us" and
; "the card never sent it". UNETESP puts the tail of the last unparsed AT
; response there instead. Writes an empty string when no DLL is loaded.
; ==========================================================================
_unet_lasterr::
        ld      (uc_rptr), hl           ; keep dst for the not-loaded path
        ld      (g_regs + 1), hl        ; de field = dst
        ld      (g_regs + 3), de        ; ix field = max
        xor     a
        ld      (g_regs), a
        ld      (g_regs + 5), a
        ld      (g_regs + 6), a
        call    _unet_loaded
        or      a
        jr      nz, ulast_go
        ld      hl, (uc_rptr)
        ld      (hl), a                 ; A is 0 here: empty string
        ld      a, #NERR_STATE
        ret
ulast_go:
        ld      a, #FN_LASTERR
        ld      de, #g_regs
        jp      _unet_call

; ==========================================================================
; unet_close(void)
; ==========================================================================
_unet_close::
        call    _unet_loaded
        or      a
        jr      nz, uclose_go
        ld      a, #NERR_STATE
        neg
        ret
uclose_go:
        ld      a, #FN_CLOSE
        jp      uc_call0

; ==========================================================================
; unet_netinit(void) / unet_netdone(void)
; ==========================================================================
_unet_netinit::
        call    _unet_loaded
        or      a
        jr      nz, unetinit_go
        ld      a, #NERR_STATE
        neg
        ret
unetinit_go:
        ld      a, #FN_NETINIT
        jp      uc_call0

_unet_netdone::
        call    _unet_loaded
        or      a
        jr      nz, unetdone_go
        ld      a, #NERR_STATE
        neg
        ret
unetdone_go:
        ld      a, #FN_NETDONE
        jp      uc_call0

; ==========================================================================
; unet_netstatus(u16 *bits) -- bits in HL.
; ==========================================================================
_unet_netstatus::
        ld      (uc_bitsptr), hl
        call    _unet_loaded
        or      a
        jr      nz, unst_go
        ld      hl, (uc_bitsptr)
        ld      a, h
        or      l
        jr      z, unst_err_nobits
        xor     a
        ld      (hl), a
        inc     hl
        ld      (hl), a
unst_err_nobits:
        ld      a, #NERR_STATE
        neg
        ret
unst_go:
        ld      hl, #g_regs
        ld      (hl), #0xFF
        inc     hl
        xor     a
        ld      (hl), a
        inc     hl
        ld      (hl), a
        inc     hl
        ld      (hl), a
        inc     hl
        ld      (hl), a
        inc     hl
        ld      (hl), a
        ld      a, #FN_STATUS
        ld      de, #g_regs
        call    _unet_call
        push    af
        ld      hl, (uc_bitsptr)
        ld      a, h
        or      l
        jr      z, unst_nobits2
        ld      de, (g_regs + 1)
        ld      (hl), e
        inc     hl
        ld      (hl), d
unst_nobits2:
        pop     af
        ret

; ==========================================================================
; unet_getinfo(u8 field, char *dst, u16 max)
; field in A, dst in DE, max at entry SP+2.
; ==========================================================================
_unet_getinfo::
        ld      hl, #2
        add     hl, sp
        ld      c, (hl)
        inc     hl
        ld      b, (hl)
        ld      (g_regs + 3), bc        ; ix field = max
        ld      (g_regs + 1), de        ; de field = dst
        ld      (g_regs), a             ; a field = field
        xor     a
        ld      (g_regs + 5), a
        ld      (g_regs + 6), a
        call    _unet_loaded
        or      a
        jr      nz, ugi_go
        ld      a, #NERR_STATE
        neg
        jr      ugi_return
ugi_go:
        ld      a, #FN_GETINFO
        ld      de, #g_regs
        call    _unet_call
ugi_return:
        ; callee-cleans-up: discard the 2 bytes (max) our caller pushed --
        ; see unet_recv's own comment on this convention.
        pop     iy
        ld      hl, #2
        add     hl, sp
        ld      sp, hl
        jp      (iy)
