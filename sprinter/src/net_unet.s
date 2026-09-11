; net_unet.s - net.h backend driven by a UNET DLL (unetldcore.s/unetcore.s)
; instead of the native ESP driver (net_esp.c/uart.c/isauart.c). Hand-written
; Z80 asm replacing net_unet.c (same algorithm; see the rationale below).
; Selected by building with NET_BACKEND=unet; irc.c/main.c never change,
; since this exposes exactly net.h's C-callable entry points.
;
; Two things this file has to get right that the native ESP backend never
; had to worry about:
;
;   - WIN1 buffer safety. unet_call() (unetcore.s/unetcall.s) swaps WIN1 to
;     the DLL for the span of every call. irc.c passes plenty of string
;     LITERALS straight into net_send()/net_send_parts() (e.g. "PING
;     :keepalive\r\n"), and SDCC places string literals in _CODE -- which is
;     exactly the window the DLL displaces. Every outgoing line is therefore
;     staged into nu_line (a _DATA/WIN2 buffer) before it is ever handed to
;     unet_send(); the same goes for host/port in net_connect().
;   - RX ordering across SEND retries. UNETAPI.md's send contract expects the
;     caller to drain RX before a SEND that might be sitting behind buffered
;     data (NERR_BUSY), and NERR_SEND can mean a transient full-duplex race
;     that a drain-and-resend clears (see fido/bink's UNET_SEND_ALL and
;     Shatranj's net_send_raw send ladder, both documented in the port plan).
;     A naive "drain and discard" would lose IRC bytes that arrived during a
;     send, so RX pulled during a send retry goes into the same small ring
;     buffer net_poll() drains from, in arrival order.
;
; sdcccall(1) conventions relied on below (see unetcore.s's header for how
; these were verified): 1-arg ptr->HL, 1-arg u8->A; 2-arg (ptr,ptr) or
; (ptr,u16) -> slot1 HL, slot2 DE; slot3+ pushed right-to-left, 2 bytes each,
; at SP+2/SP+4/SP+6 (net_send_parts has three such args: c,d,e). Stack-passed
; arguments are CALLEE-CLEANED (discarded by the callee before it returns,
; like Pascal/stdcall -- verified against SDCC's actual codegen; the SDK's
; own dss_open/dss_read/dss_seek use the identical pattern). net_send_parts
; below is this file's only function with stack args, so it is this file's
; only place that needs that cleanup epilogue. unet_getinfo (in unetcore.s)
; also takes one stack arg (max) and self-cleans it the same way, so callers
; here never pop after calling it either.

        .module net_unet

        .globl _net_init
        .globl _net_connect
        .globl _net_send
        .globl _net_send_parts
        .globl _net_poll
        .globl _net_close
        .globl _net_is_connected
        .globl _net_stalled
        .globl _net_clear_stall
        .globl _net_overrun
        .globl _net_clear_overrun
        .globl _net_cfg_baud
        .globl _net_cfg_div
        .globl _net_cfg_slot
        .globl _net_last_status
        .globl _net_last_detail

        .globl _unetld_select
        .globl _unetld_load
        .globl _unetld_require
        .globl _unetld_netstart
        .globl _unetld_unload

        .globl _unet_connect
        .globl _unet_send
        .globl _unet_recv
        .globl _unet_close
        .globl _unet_netdone
        .globl _unet_loaded
        .globl _unet_getinfo
        .globl _unet_lasterr

NET_OK          = 0
NET_NO_HW       = -1
NET_NO_LINK     = -2
UNET_CAP_TCP    = 0x0001
UNET_IF_BAUD    = 9
UNET_IF_HW      = 12
NERR_OK         = 0
NERR_CLOSED     = 7
NERR_BUSY       = 13
NERR_SEND       = 5

RX_RING_SIZE    = 256           ; a byte-aligned size: "count < 256" is just "high byte == 0"
RX_RING_MASK    = RX_RING_SIZE - 1
; Retry budget for one outgoing line. nu_retry counts only attempts that made
; NO progress (NERR_BUSY, or a partial send of zero bytes); a partial send that
; actually moved bytes clears it, so a large line is never cut short. Keep this
; small: on a dead link every attempt costs a full TCP retransmit timeout
; inside the DLL, and 20 of those is minutes of a frozen UI -- which is exactly
; what "network not responding after a long delay" was.
SEND_BUSY_RETRY_MAX     = 6
SEND_RESEND_RETRY_MAX   = 1

; ==========================================================================
; State (zeroed at start by gsinit -- crt0_win0.s via gsinit_zero_data.s).
; ==========================================================================
        .area _DATA
nu_connected:   .ds 1
nu_stall:       .ds 1
nu_status:      .ds 1           ; last NERR_* from CONNECT/SEND, for the UI
nu_overrun:     .ds 1
nu_rxhead:      .ds 2
nu_rxtail:      .ds 2
nu_rxcount:     .ds 2
rx_ring:        .ds RX_RING_SIZE
nu_host:        .ds 130          ; UNET caps host at 128 bytes; +NUL +1 slack
nu_port:        .ds 16           ; UNET caps port at 15 bytes
nu_line:        .ds 520          ; staged outgoing IRC line (irc.c's own asmbuf is 512)
nu_tmp:         .ds 96           ; pump_rx's scratch receive buffer
nu_flags:       .ds 2
nu_rounds:      .ds 1
nu_sendbuf:     .ds 2
nu_sendlen:     .ds 2
nu_retry:       .ds 1
nu_resend:      .ds 1
nu_pp_a:        .ds 2
nu_pp_b:        .ds 2
nu_pp_c:        .ds 2
nu_pp_d:        .ds 2
nu_pp_e:        .ds 2
nu_appendpos:   .ds 2
nu_baudbuf:     .ds 16
nu_detail:      .ds 80          ; LASTERR line (UNETRTL formats 72 bytes)
nrp_srcptr:     .ds 2
nrp_remain:     .ds 2
nrpop_dst:      .ds 2
nrpop_max:      .ds 2
nrpop_n:        .ds 2

        .area _CODE

; ==========================================================================
; nu_copy_bounded: HL=src (may be NULL), DE=dst, BC=max. Copies up to max-1
; bytes, NUL-terminating dst. Clobbers AF,HL,DE,BC.
; ==========================================================================
nu_copy_bounded:
        ld      a, h
        or      l
        jr      nz, ncb_have_src
        xor     a
        ld      (de), a
        ret
ncb_have_src:
        dec     bc
ncb_loop:
        ld      a, b
        or      c
        jr      z, ncb_done
        ld      a, (hl)
        or      a
        jr      z, ncb_done
        ld      (de), a
        inc     hl
        inc     de
        dec     bc
        jr      ncb_loop
ncb_done:
        xor     a
        ld      (de), a
        ret

; nu_strlen: HL=str -> DE=length. Clobbers AF,DE (HL preserved).
nu_strlen:
        push    hl
        ld      de, #0
nsl2_loop:
        ld      a, (hl)
        or      a
        jr      z, nsl2_done
        inc     hl
        inc     de
        jr      nsl2_loop
nsl2_done:
        pop     hl
        ret

; ==========================================================================
; RX ring buffer (byte queue, net_poll()'s own storage). All state lives in
; memory, not registers, across these loops -- this path isn't a hot loop
; (a handful of bytes per main-loop iteration), so trading a few extra
; memory accesses for straightforward, easy-to-check correctness is the
; right call given there is no way to test this on real hardware from here.
; ==========================================================================
nu_rx_reset:
        ld      hl, #0
        ld      (nu_rxhead), hl
        ld      (nu_rxtail), hl
        ld      (nu_rxcount), hl
        ret

; nu_rx_push: HL=data, DE=n.
nu_rx_push:
        ld      a, d
        or      e
        ret     z
        ld      (nrp_srcptr), hl
        ld      (nrp_remain), de
nrp_loop:
        ld      a, (nu_rxcount + 1)     ; RX_RING_SIZE=256: count<256 iff high byte==0
        or      a
        jr      nz, nrp_full
        ld      hl, (nrp_srcptr)
        ld      a, (hl)
        ld      hl, (nu_rxtail)
        ld      de, #rx_ring
        add     hl, de
        ld      (hl), a
        ld      hl, (nu_rxtail)
        inc     hl
        ld      a, l
        and     #(RX_RING_MASK & 0xFF)
        ld      l, a
        ld      h, #0
        ld      (nu_rxtail), hl
        ld      hl, (nu_rxcount)
        inc     hl
        ld      (nu_rxcount), hl
        ld      hl, (nrp_srcptr)
        inc     hl
        ld      (nrp_srcptr), hl
        ld      hl, (nrp_remain)
        dec     hl
        ld      (nrp_remain), hl
        ld      a, h
        or      l
        jr      nz, nrp_loop
        ret
nrp_full:
        ld      a, #1
        ld      (nu_overrun), a
        ret

; nu_rx_pop: HL=dst, DE=max. Out: DE = bytes copied.
nu_rx_pop:
        ld      (nrpop_dst), hl
        ld      (nrpop_max), de
        ld      hl, #0
        ld      (nrpop_n), hl
nrpop_loop:
        ld      hl, (nrpop_n)
        ld      de, (nrpop_max)
        or      a
        sbc     hl, de
        jr      nc, nrpop_done          ; n >= max
        ld      hl, (nu_rxcount)
        ld      a, h
        or      l
        jr      z, nrpop_done           ; nothing buffered
        ld      hl, (nu_rxhead)
        ld      de, #rx_ring
        add     hl, de
        ld      a, (hl)
        ld      hl, (nrpop_dst)
        ld      (hl), a
        inc     hl
        ld      (nrpop_dst), hl
        ld      hl, (nrpop_n)
        inc     hl
        ld      (nrpop_n), hl
        ld      hl, (nu_rxhead)
        inc     hl
        ld      a, l
        and     #(RX_RING_MASK & 0xFF)
        ld      l, a
        ld      h, #0
        ld      (nu_rxhead), hl
        ld      hl, (nu_rxcount)
        dec     hl
        ld      (nu_rxcount), hl
        jr      nrpop_loop
nrpop_done:
        ld      de, (nrpop_n)
        ret

; ==========================================================================
; nu_pump_rx: pull whatever the DLL has buffered for channel 0 into the ring,
; non-blocking (timeout 0). Bounded rounds so a continuous flood can't starve
; the caller. Clobbers everything.
; ==========================================================================
nu_pump_rx:
        ld      a, (nu_connected)
        or      a
        ret     z
        ld      a, #4
        ld      (nu_rounds), a
npr_loop:
        ld      a, (nu_rounds)
        or      a
        ret     z
        dec     a
        ld      (nu_rounds), a

        ld      hl, #RX_RING_SIZE
        ld      de, (nu_rxcount)
        or      a
        sbc     hl, de                  ; hl = room
        ld      de, #96
        or      a
        sbc     hl, de                  ; hl = room - 96
        jr      c, npr_room_small
        ld      hl, #96
        jr      npr_max_ready
npr_room_small:
        add     hl, de                  ; hl = room again
npr_max_ready:
        ld      a, h
        or      l
        ret     z                       ; max == 0: ring full, net_poll() drains it

        ex      de, hl                  ; de = max
        ld      hl, #nu_flags
        push    hl                      ; flags ptr (slot4)
        ld      hl, #0
        push    hl                      ; timeout=0 (slot3)
        ld      hl, #nu_tmp
        call    _unet_recv              ; hl=buf, de=max already set; self-cleans its 4 stack bytes
        ld      a, d
        add     a, a
        jr      c, npr_neg              ; DE negative -> -(i16)status
        ld      a, d
        or      e
        jr      z, npr_idle             ; DE == 0: nothing right now
        push    de
        ld      hl, #nu_tmp
        call    nu_rx_push
        pop     de
        ld      hl, (nu_flags)
        bit     2, l                    ; UNET_RXF_LOST
        jr      z, npr_check_more
        ld      a, #1
        ld      (nu_overrun), a
npr_check_more:
        ld      hl, (nu_flags)
        bit     1, l                    ; UNET_RXF_MORE
        jr      nz, npr_loop
        ret
npr_idle:
        ret
npr_neg:
        ld      a, e
        neg
        cp      #NERR_CLOSED
        jr      nz, npr_ret
        xor     a
        ld      (nu_connected), a
npr_ret:
        ret

; ==========================================================================
; nu_send_line: HL=buf, DE=len. Drains RX before every SEND attempt, retries
; on NERR_BUSY, bounded-resends on NERR_SEND, gives up (sets nu_stall) after
; SEND_BUSY_RETRY_MAX rounds. Clobbers everything.
; ==========================================================================
nu_send_line:
        ld      (nu_sendbuf), hl
        ld      (nu_sendlen), de
        ld      a, (nu_connected)
        or      a
        ret     z
        ld      hl, (nu_sendlen)
        ld      a, h
        or      l
        ret     z
        ld      a, (nu_stall)
        or      a
        ret     nz                      ; a send already gave up and nothing has
                                        ; arrived since: don't pay the whole
                                        ; ladder again for every following line
                                        ; (irc_register sends NICK and USER back
                                        ; to back). main.c re-arms the flag once
                                        ; per main-loop pass, so a link that
                                        ; comes back is still retried.
        xor     a
        ld      (nu_retry), a
        ld      (nu_resend), a
nsl_loop:
        ld      a, (nu_retry)
        cp      #SEND_BUSY_RETRY_MAX
        jr      nc, nsl_stall
        call    nu_pump_rx
        ld      hl, (nu_sendbuf)
        ld      de, (nu_sendlen)
        call    _unet_send
        ld      a, d
        add     a, a
        jr      c, nsl_error
        ld      hl, (nu_sendlen)
        or      a
        sbc     hl, de                  ; hl = len - sent
        jr      c, nsl_full_sent
        jr      z, nsl_full_sent
        ld      (nu_sendlen), hl
        ld      hl, (nu_sendbuf)
        add     hl, de
        ld      (nu_sendbuf), hl
        ld      a, d                    ; did this round move any bytes?
        or      e
        jr      z, nsl_no_progress
        xor     a                       ; yes: the link is alive, so the rest
        ld      (nu_retry), a           ; of the line gets a fresh budget
        jr      nsl_loop
nsl_no_progress:
        ld      a, (nu_retry)
        inc     a
        ld      (nu_retry), a
        jr      nsl_loop
nsl_full_sent:
        xor     a
        ld      (nu_stall), a
        ld      (nu_status), a
        ret
nsl_error:
        ld      a, e
        neg
        ld      (nu_status), a
        cp      #NERR_CLOSED
        jr      nz, nsl_not_closed
        xor     a
        ld      (nu_connected), a
        ret
nsl_not_closed:
        cp      #NERR_BUSY
        jr      nz, nsl_not_busy
        ld      a, (nu_retry)
        inc     a
        ld      (nu_retry), a
        jr      nsl_loop
nsl_not_busy:
        cp      #NERR_SEND
        jr      nz, nsl_fatal
        ld      a, (nu_resend)
        cp      #SEND_RESEND_RETRY_MAX
        jr      nc, nsl_fatal
        inc     a
        ld      (nu_resend), a
        ld      a, (nu_retry)
        inc     a
        ld      (nu_retry), a
        jr      nsl_loop
nsl_fatal:
nsl_stall:
        ld      a, #1
        ld      (nu_stall), a
        ret

; ==========================================================================
; net_init(void)
; ==========================================================================
_net_init::
        xor     a
        ld      (nu_connected), a
        ld      (nu_stall), a
        ld      (nu_overrun), a
        call    nu_rx_reset
        call    _unetld_select
        or      a
        jr      z, ni_select_ok
        ld      a, #NET_NO_LINK
        ret
ni_select_ok:
        call    _unetld_load
        or      a
        jr      z, ni_load_ok
        ld      a, #NET_NO_HW
        ret
ni_load_ok:
        ld      hl, #UNET_CAP_TCP
        call    _unetld_require
        or      a
        jr      nz, ni_require_ok
        call    _unetld_unload
        ld      a, #NET_NO_HW
        ret
ni_require_ok:
        call    _unetld_netstart
        or      a
        jr      z, ni_ok
        call    _unetld_unload
        ld      a, #NET_NO_LINK
        ret
ni_ok:
        xor     a
        ret

; ==========================================================================
; net_connect(const char *host, const char *port) -- host in HL, port in DE.
; ==========================================================================
_net_connect::
        push    de
        ld      de, #nu_host
        ld      bc, #130
        call    nu_copy_bounded
        pop     hl
        ld      de, #nu_port
        ld      bc, #16
        call    nu_copy_bounded
        xor     a
        ld      (nu_connected), a
        call    nu_rx_reset
        ld      hl, #nu_host
        ld      de, #nu_port
        call    _unet_connect
        ld      (nu_status), a
        or      a
        jr      z, nc_ok
        ld      a, #NET_NO_LINK
        ret
nc_ok:
        ld      a, #1
        ld      (nu_connected), a
        xor     a
        ret

; ==========================================================================
; net_send(const char *s) -- s in HL.
; ==========================================================================
_net_send::
        ld      a, h
        or      l
        jr      nz, ns_have
        ld      hl, #nu_line
        xor     a
        ld      (hl), a
        ld      de, #0
        jp      nu_send_line
ns_have:
        ld      de, #nu_line
        ld      bc, #520
        call    nu_copy_bounded
        ld      hl, #nu_line
        call    nu_strlen               ; -> DE = length
        ld      hl, #nu_line
        jp      nu_send_line

; ==========================================================================
; net_send_parts(a,b,c,d,e) -- a in HL, b in DE, c/d/e at entry SP+2/+4/+6.
; The only function in this file with stack-passed args: self-cleans 6 bytes
; before returning (see this file's header).
; ==========================================================================
_net_send_parts::
        ld      (nu_pp_a), hl
        ld      (nu_pp_b), de
        ld      hl, #2
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (nu_pp_c), de
        ld      hl, #4
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (nu_pp_d), de
        ld      hl, #6
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      (nu_pp_e), de

        ld      hl, #0
        ld      (nu_appendpos), hl
        ld      hl, (nu_pp_a)
        call    nu_append
        ld      hl, (nu_pp_b)
        call    nu_append
        ld      hl, (nu_pp_c)
        call    nu_append
        ld      hl, (nu_pp_d)
        call    nu_append
        ld      hl, (nu_pp_e)
        call    nu_append

        ld      hl, (nu_appendpos)
        ld      de, #nu_line
        add     hl, de
        xor     a
        ld      (hl), a

        ld      hl, #nu_line
        ld      de, (nu_appendpos)
        call    nu_send_line

        ; callee-cleans-up: discard the 6 bytes (c,d,e) our caller pushed.
        pop     iy
        ld      hl, #6
        add     hl, sp
        ld      sp, hl
        jp      (iy)

; nu_append: HL=source ptr (may be NULL). Appends chars to nu_line at
; nu_appendpos (bounded to 519, leaving room for the NUL) until the source's
; own NUL. Clobbers AF,HL,DE.
nu_append:
        ld      a, h
        or      l
        ret     z
nap_loop:
        ld      a, (hl)
        or      a
        ret     z
        push    hl
        ld      hl, (nu_appendpos)
        ld      de, #519
        or      a
        sbc     hl, de
        jr      nc, nap_full
        ld      hl, (nu_appendpos)
        ld      de, #nu_line
        add     hl, de
        ld      (hl), a
        ld      hl, (nu_appendpos)
        inc     hl
        ld      (nu_appendpos), hl
        pop     hl
        inc     hl
        jr      nap_loop
nap_full:
        pop     hl
        ret

; ==========================================================================
; net_poll(u8 *buf, u16 max) -- buf in HL, max in DE. Out: DE = bytes copied.
; ==========================================================================
_net_poll::
        push    hl
        push    de
        call    nu_pump_rx
        pop     de
        pop     hl
        jp      nu_rx_pop

; ==========================================================================
; net_close(void)
; ==========================================================================
; Closes the SOCKET only. NETDONE belongs to program shutdown (main.c's
; unetld_unload), not here: net_close() is also the /close command and the
; keepalive timeout's teardown, and after a NETDONE the DLL answers every
; later CONNECT with NERR_STATE -- one dropped link would mean no reconnect
; for the rest of the session.
_net_close::
        ld      a, (nu_connected)
        or      a
        jr      z, ncl_finish
        call    _unet_close
ncl_finish:
        xor     a
        ld      (nu_connected), a
        jp      nu_rx_reset

; ==========================================================================
; Simple flag accessors.
; ==========================================================================
_net_is_connected::
        ld      a, (nu_connected)
        ret
_net_stalled::
        ld      a, (nu_stall)
        ret
; Last NERR_* the DLL returned from CONNECT or SEND. main.c prints it, so a
; failure names itself instead of arriving as a bare "network not responding".
_net_last_status::
        ld      a, (nu_status)
        ret

; ==========================================================================
; net_last_detail(void) -- refresh and return the DLL's own LASTERR line.
; Out: HL = nu_detail (always a valid ASCIIZ string, empty if unavailable).
; ==========================================================================
_net_last_detail::
        ld      hl, #nu_detail
        ld      de, #80
        call    _unet_lasterr
        ld      de, #nu_detail          ; sdcccall(1) returns 16 bits in DE
        ret
_net_clear_stall::
        xor     a
        ld      (nu_stall), a
        ret
_net_overrun::
        ld      a, (nu_overrun)
        ret
_net_clear_overrun::
        xor     a
        ld      (nu_overrun), a
        ret

; ==========================================================================
; Diagnostic getters (main.c's startup banner). UNET hides the UART divisor
; entirely -- net_cfg_div has no UNET equivalent, always 0; baud/slot come
; from GETINFO, meaningful only once NETINIT has actually run.
; ==========================================================================
_net_cfg_baud::
        xor     a
        ld      (nu_baudbuf), a
        call    _unet_loaded
        or      a
        jr      z, ncfgb_ret
        ld      hl, #16
        push    hl                      ; max (unet_getinfo's stack arg)
        ld      de, #nu_baudbuf
        ld      a, #UNET_IF_BAUD
        call    _unet_getinfo           ; self-cleans its own 2 stack bytes
ncfgb_ret:
        ld      de, #nu_baudbuf
        ret

_net_cfg_div::
        xor     a
        ret

_net_cfg_slot::
        call    _unet_loaded
        or      a
        jr      z, ncfgs_zero
        ld      hl, #16
        push    hl
        ld      de, #nu_baudbuf         ; reused as scratch: net_cfg_baud/net_cfg_slot are one-shot
                                          ; getters called back-to-back from main.c's startup banner,
                                          ; never concurrently
        ld      a, #UNET_IF_HW
        call    _unet_getinfo
        cp      #NERR_OK
        jr      nz, ncfgs_zero
        ld      a, (nu_baudbuf)
        cp      #'0'
        jr      c, ncfgs_zero
        cp      #'9' + 1
        jr      nc, ncfgs_zero
        sub     #'0'
        ret
ncfgs_zero:
        xor     a
        ret
