; unetldcore.s - UNET backend selector/loader (UNETLD-SPEC.md), hand-written
; Z80 asm replacing unetld.c. Ported from the proven reference implementation
; at .../sources/unet_libs/unet_libs_asm/include/unetld.asm (SjASMPlus) to
; sdasz80 syntax and SDCC 4.5's sdcccall(1) C linkage, so net_unet.c calls
; these exactly as it called the C version -- no other file changes.
;
; SELECT/NETSTART/REQUIRE/UNLOAD below are a close, deliberate port of that
; reference's algorithm and register-level technique (same upper-casing loop,
; same alias-table walk, same capability-mask-and-compare). LOAD differs from
; the reference because this port does not use libman: it resolves the DLL
; path itself, an enhancement over the base UNETLD-SPEC uses by NETUP/
; UNETTEST, and calls unetcore.s's unet_load (the L1 stager) instead of
; LIBMAN.l_load/l_info. The path is read from P0:0x0100 (WIN0 = P0 in the
; resting state, so this is a plain memory read), staged there by the win0
; stage-1 loader (lib/win0/loader.c) -- NOT via DSS APPINFO, which that
; loader's own comment warns corrupts/hangs a nested-dss_exec'd program.
;
; sdcccall(1) conventions relied on throughout (verified against SDCC 4.5's
; actual codegen -- see the port plan's notes, not assumed from docs):
;   argument slot 1: A if u8, HL if 16-bit/pointer
;   argument slot 2: DE (8-bit values also arrive as a single pushed byte
;                     instead, but nothing here has an 8-bit slot 2)
;   argument slot 3+: pushed right-to-left, 2 bytes each, so at callee entry
;                     (before any of the callee's own pushes) slot 3 is at
;                     SP+2, slot 4 at SP+4, etc.
;   return: A for u8/i8, DE for u16/i16/pointers.

        .module unetldcore

        .globl _unetld_reset
        .globl _unetld_select
        .globl _unetld_load
        .globl _unetld_netstart
        .globl _unetld_require
        .globl _unetld_unload
        .globl _unetld_net_tag
        .globl _unetld_dll_name
        .globl _unetld_dll_path
        .globl _unetld_error
        .globl _unetld_last_status
        .globl _unetld_flags

        ; unet.c (unetcore.s) entry points this module calls into.
        .globl _unet_load
        .globl _unet_free
        .globl _unet_loaded
        .globl _unet_dll_name
        .globl _unet_caps
        .globl _unet_abi
        .globl _unet_call

        ; SDK helpers (sprinter.lib), called with their own naked/sdcccall(1)
        ; conventions -- see .../lib/src/dss/dss_getenv.c.
        .globl _dss_getenv

; ==========================================================================
; State (zeroed at program start by gsinit -- _DATA is a BSS-like area here;
; UNETLD_E_NONE/no-flags/empty-strings all read as 0, which is the correct
; "nothing loaded yet" state with no explicit RESET call needed, matching
; the reference's "Simple mode").
; ==========================================================================
        .area _DATA
u_tag:          .ds 5           ; resolved tag, ASCIIZ, up to 4 chars + NUL
u_dllname:      .ds 13          ; "UNETxxxx.DLL",0
u_error:        .ds 1           ; last UNETLD_E_*
u_laststat:     .ds 1           ; last NERR_* from STATUS/NETINIT/GETCAPS
u_flags:        .ds 1           ; UNETLD_F_*
u_dllpath:      .ds 112         ; app dir (<=94, capped by lib/win0/loader.c)
                                ; + '\' + "UNETxxxx.DLL" + NUL
u_envval:       .ds 256         ; raw/upper-cased NET value (DSS ENV_GET needs 256)
u_regs:         .ds 7           ; scratch unet_regs {a,de,ix,iy} for calls below

        .area _CODE

; ==========================================================================
; unetld_reset(void) -- zero the whole state block via LDIR.
; ==========================================================================
_unetld_reset::
        ld      hl, #u_tag
        ld      de, #u_tag + 1
        ld      bc, #(u_regs - u_tag) - 1
        xor     a
        ld      (hl), a
        ldir
        ret

; ==========================================================================
; unetld_select(void) -- read NET, validate, resolve, build DLL_NAME.
; Port of unetld.asm's SELECT (see that file for the line-by-line original).
; Out: A = 0 on success; A = -UNETLD_E_NOENV / -UNETLD_E_BADVALUE on failure
;      (unetld.h's error codes are small positive ints; the C API returns
;      them negated as i8, so we negate here to match unet_ldh.h exactly).
; ==========================================================================
_unetld_select::
        call    _unetld_reset
        ld      hl, #lit_net
        ld      de, #u_envval
        call    _dss_getenv             ; sdcccall(1): name->HL, buf->DE; A=0 ok, 0xFF fail
        or      a
        jp      nz, uls_noenv           ; jr: uls_noenv is out of range from here
        ld      a, (u_envval)
        or      a
        jp      z, uls_noenv

        ; Upper-case in place, measuring length in B.
        ld      hl, #u_envval
        ld      b, #0
uls_upper:
        ld      a, (hl)
        or      a
        jr      z, uls_upper_done
        cp      #'a'
        jr      c, uls_not_lower
        cp      #'z' + 1
        jr      nc, uls_not_lower
        sub     #32
        ld      (hl), a
uls_not_lower:
        inc     hl
        inc     b
        jr      uls_upper
uls_upper_done:
        ld      a, b
        cp      #3
        jr      c, uls_badvalue
        cp      #5
        jr      nc, uls_badvalue

        ; Charset: every char must be 0-9 or A-Z.
        ld      hl, #u_envval
uls_charset:
        ld      a, (hl)
        or      a
        jr      z, uls_charset_ok
        cp      #'0'
        jr      c, uls_badvalue
        cp      #'9' + 1
        jr      c, uls_charset_next
        cp      #'A'
        jr      c, uls_badvalue
        cp      #'Z' + 1
        jr      nc, uls_badvalue
uls_charset_next:
        inc     hl
        jr      uls_charset
uls_charset_ok:

        ; Alias lookup: one built-in entry, WIFI -> ESP (see unetld.h). No
        ; match -> the validated value itself is the tag.
        ld      hl, #u_envval
        ld      de, #lit_wifi
        call    uls_streq
        jr      nz, uls_no_alias
        ld      hl, #lit_esp
        ld      de, #u_tag
        call    uls_strcpy
        jr      uls_tag_ready
uls_no_alias:
        ld      hl, #u_envval
        ld      de, #u_tag
        call    uls_strcpy
uls_tag_ready:

        ; DLL_NAME = "UNET" + tag + ".DLL", 0
        ld      hl, #u_dllname
        ld      (hl), #'U'
        inc     hl
        ld      (hl), #'N'
        inc     hl
        ld      (hl), #'E'
        inc     hl
        ld      (hl), #'T'
        inc     hl
        ld      de, #u_tag
uls_append_tag:
        ld      a, (de)
        or      a
        jr      z, uls_append_done
        ld      (hl), a
        inc     hl
        inc     de
        jr      uls_append_tag
uls_append_done:
        ld      (hl), #'.'
        inc     hl
        ld      (hl), #'D'
        inc     hl
        ld      (hl), #'L'
        inc     hl
        ld      (hl), #'L'
        inc     hl
        ld      (hl), #0

        xor     a
        ld      (u_error), a
        ret

uls_noenv:
        ld      a, #1                   ; UNETLD_E_NOENV
        jr      uls_fail
uls_badvalue:
        ld      a, #2                   ; UNETLD_E_BADVALUE
uls_fail:
        ld      (u_error), a
        neg
        ret

; Compare ASCIIZ HL and DE (case-sensitive). Z if equal. Clobbers A,HL,DE.
uls_streq:
        ld      a, (de)
        cp      (hl)
        ret     nz
        or      a
        ret     z
        inc     hl
        inc     de
        jr      uls_streq

; Copy ASCIIZ HL to DE, including the NUL. Clobbers A,HL,DE.
uls_strcpy:
        ld      a, (hl)
        ld      (de), a
        or      a
        ret     z
        inc     hl
        inc     de
        jr      uls_strcpy

lit_net:        .ascii "NET"
                 .db 0
lit_wifi:        .ascii "WIFI"
                 .db 0
lit_esp:         .ascii "ESP"
                 .db 0

; ==========================================================================
; unetld_load(void) -- resolve the DLL path beside the EXE (from P0:0x0100,
; staged by the win0 loader), falling back to the bare name, then unet_load()
; + validate the DLL's own name against the resolved tag + GETCAPS/ABI
; (already done inside unet_load itself; here we only need the name check +
; a GETCAPS re-read to populate LAST_STATUS/FLAGS the way the reference's
; LOAD does).
; Out: A = 0, or -UNETLD_E_LOAD / -UNETLD_E_NAME / -UNETLD_E_CALL.
; ==========================================================================
_unetld_load::
        ld      a, (u_tag)
        or      a
        jr      nz, ull_have_tag
        ld      a, #1                   ; UNETLD_E_NOENV: SELECT was never called
        ld      (u_error), a
        neg
        ret
ull_have_tag:
        ld      hl, #0x0100             ; app directory, staged by lib/win0/loader.c
        ld      a, (hl)
        or      a
        jr      z, ull_bare_name
        ld      de, #u_dllpath
        call    uls_strcpy

        ; u_dllpath = dir + '\' (if not already there) + u_dllname
        ld      hl, #u_dllpath
        ld      b, #0
ull_dirlen:
        ld      a, (hl)
        or      a
        jr      z, ull_dirlen_done
        inc     hl
        inc     b
        jr      ull_dirlen
ull_dirlen_done:
        ld      a, b
        or      a
        jr      z, ull_bare_name
        dec     hl
        ld      a, (hl)
        cp      #0x5C                   ; '\'
        inc     hl
        jr      z, ull_have_sep
        ld      (hl), #0x5C             ; '\'
        inc     hl
ull_have_sep:
        ex      de, hl                  ; de = insertion point in u_dllpath (dest)
        ld      hl, #u_dllname          ; hl = source
        call    uls_strcpy              ; copies u_dllname -> u_dllpath's tail
        ld      hl, #u_dllpath
        jr      ull_try_load
ull_bare_name:
        ld      hl, #u_dllname
ull_try_load:
        call    _unet_load              ; sdcccall(1): path->HL; A=UNET_LOAD_*
        or      a
        jr      z, ull_loaded_ok
        ld      a, #3                   ; UNETLD_E_LOAD
        ld      (u_error), a
        neg
        ret

ull_loaded_ok:
        ; Validate the DLL's self-reported name (unet_dll_name(), cached by
        ; unet_load from the L1 header) against "UNET" + our tag.
        call    _unet_dll_name          ; -> DE = pointer to cached name
        ex      de, hl
        ld      de, #lit_unet4
        ld      b, #4
ull_cmp_unet:
        ld      a, (de)
        cp      (hl)
        jr      nz, ull_bad_name
        inc     hl
        inc     de
        djnz    ull_cmp_unet
        ld      de, #u_tag
ull_cmp_tag:
        ld      a, (de)
        or      a
        jr      z, ull_name_ok
        cp      (hl)
        jr      nz, ull_bad_name
        inc     hl
        inc     de
        jr      ull_cmp_tag
ull_bad_name:
        call    _unet_free
        ld      a, #4                   ; UNETLD_E_NAME
        ld      (u_error), a
        neg
        ret
ull_name_ok:
        ; GETCAPS was already validated (ABI major + CAP_TCP) inside
        ; unet_load; re-read it here only to record LAST_STATUS, mirroring
        ; the reference's own post-load GETCAPS call.
        ld      a, #2                   ; UNET_FN_GETCAPS
        ld      de, #u_regs
        call    ull_call0
        ld      (u_laststat), a
        or      a
        jr      nz, ull_call_failed
        ld      a, (u_flags)
        or      #1                      ; UNETLD_F_LOADED
        ld      (u_flags), a
        xor     a
        ld      (u_error), a
        ret
ull_call_failed:
        call    _unet_free
        ld      a, #5                   ; UNETLD_E_CALL
        ld      (u_error), a
        neg
        ret

lit_unet4:      .ascii "UNET"           ; 4 bytes, compared without a NUL check

; ==========================================================================
; unetld_netstart(void) -- STATUS(0xFF) then NETINIT. Port of the
; reference's NETSTART, calling this port's own unet_call instead of
; UNETLD.CALL/libman.
; Out: A = 0, or -UNETLD_E_CALL / -UNETLD_E_STATUS / -UNETLD_E_NETINIT.
; ==========================================================================
_unetld_netstart::
        ld      a, (u_flags)
        and     #1                      ; UNETLD_F_LOADED
        jr      nz, uns_loaded
        ld      a, #6                   ; UNETLD_E_CALL: LOAD was never called
        ld      (u_error), a
        neg
        ret
uns_loaded:
        ld      hl, #u_regs
        ld      (hl), #0xFF             ; a = 0xFF (network-wide STATUS)
        inc     hl
        xor     a
        ld      (hl), a                 ; de.lo
        inc     hl
        ld      (hl), a                 ; de.hi
        inc     hl
        ld      (hl), a                 ; ix.lo
        inc     hl
        ld      (hl), a                 ; ix.hi
        inc     hl
        ld      (hl), a                 ; iy.lo
        inc     hl
        ld      (hl), a                 ; iy.hi
        ld      a, #9                   ; UNET_FN_STATUS
        ld      de, #u_regs
        call    _unet_call
        ld      (u_laststat), a
        or      a
        jr      z, uns_proceed
        cp      #2                      ; NERR_NONET is also acceptable here
        jr      z, uns_proceed
        ld      a, #7                   ; UNETLD_E_STATUS
        ld      (u_error), a
        neg
        ret
uns_proceed:
        ld      a, #3                   ; UNET_FN_NETINIT
        ld      de, #u_regs
        call    ull_call0
        ld      (u_laststat), a
        or      a
        jr      z, uns_ok
        ld      a, #8                   ; UNETLD_E_NETINIT
        ld      (u_error), a
        neg
        ret
uns_ok:
        ld      a, (u_flags)
        or      #2                      ; UNETLD_F_NETINIT
        ld      (u_flags), a
        xor     a
        ld      (u_error), a
        ret

; Zero u_regs' de/ix/iy fields, set a=(A on entry), dispatch via unet_call.
; In: A = UNET_FN_*. Out: A = NERR_* (also left in u_regs+0).
ull_call0:
        push    af
        ld      hl, #u_regs + 1
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
        ld      de, #u_regs
        jp      _unet_call

; ==========================================================================
; unetld_require(u16 mask) -- capability gate. mask arrives in HL.
; Out: A = 1 if (unet_caps() & mask) == mask, else A = 0.
; ==========================================================================
_unetld_require::
        push    hl                      ; save mask
        call    _unet_caps              ; -> DE = caps
        pop     hl
        ld      a, h                    ; (caps & mask) == MASK, byte by byte.
        and     d                       ; Comparing against the caps byte
        cp      h                       ; instead would demand caps == mask
        jr      nz, ulr_no              ; exactly, i.e. reject every DLL that
        ld      a, l                    ; offers anything beyond what was
        and     e                       ; asked for -- which is all of them
        cp      l                       ; (UNETRTL reports UDP/raw as well).
        jr      nz, ulr_no
        ld      a, #1
        ret
ulr_no:
        xor     a
        ret

; ==========================================================================
; unetld_unload(void) -- best-effort NETDONE + unet_free(), then drop the
; live-state flags. Deliberately NOT a full unetld_reset(): callers unload
; *because* something failed, and wiping u_error/u_tag/u_dllname here would
; destroy the only record of why (net_unet.s's net_init unloads on both the
; capability-gate and NETSTART failures, and main.c reports the reason). The
; next bring-up still starts clean -- unetld_select() opens with a real reset.
; ==========================================================================
_unetld_unload::
        ld      a, (u_flags)
        and     #2                      ; UNETLD_F_NETINIT
        jr      z, ulu_skip_netdone
        ld      a, #4                   ; UNET_FN_NETDONE
        ld      de, #u_regs
        call    ull_call0
ulu_skip_netdone:
        ld      a, (u_flags)
        and     #1                      ; UNETLD_F_LOADED
        jr      z, ulu_skip_free
        call    _unet_free
ulu_skip_free:
        xor     a
        ld      (u_flags), a            ; clears LOADED|NETINIT; u_dllpath/
        ret                             ; u_envval/u_regs are pure scratch

; ==========================================================================
; State accessors.
; ==========================================================================
_unetld_net_tag::
        ld      de, #u_tag
        ret
_unetld_dll_name::
        ld      de, #u_dllname
        ret
; The full path unetld_load() actually tried, app directory included. Empty
; until LOAD runs, and empty after a LOAD that fell back to the bare name.
; This is what makes a "DLL not found" report actionable.
_unetld_dll_path::
        ld      de, #u_dllpath
        ret
_unetld_error::
        ld      a, (u_error)
        ret
_unetld_last_status::
        ld      a, (u_laststat)
        ret
_unetld_flags::
        ld      a, (u_flags)
        ret
