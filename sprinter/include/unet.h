/*
 * unet.h — C binding for the UNET universal network DLL (libman 1.3 / L1 format).
 *
 * Numbers/codes below come from the frozen ABI in unet_libs_core/abi/unet_abi.toml
 * (unet_libs_core/docs/UNETAPI.md is the prose reference; unetld.h is the loader
 * layer on top of this file). Calling convention: every UNET function takes its
 * arguments only in A/DE/IX/IY and returns its status in A (0 = NERR_OK, else a
 * NERR_* code) — never test carry, the dispatcher does not propagate it except
 * for INIT. This header is backend-agnostic: the same constants drive UNETESP.DLL,
 * UNETRTL.DLL and UNET509B.DLL.
 */
#ifndef UNET_H
#define UNET_H

#include <sprinter.h>

/* ----- function numbers (index passed to unet_call) ----- */
#define UNET_FN_INIT        0
#define UNET_FN_FINI        1
#define UNET_FN_GETCAPS     2
#define UNET_FN_NETINIT     3
#define UNET_FN_NETDONE     4
#define UNET_FN_CONNECT     5
#define UNET_FN_SEND        6
#define UNET_FN_RECV        7
#define UNET_FN_CLOSE       8
#define UNET_FN_STATUS      9
#define UNET_FN_UDPOPEN     10
#define UNET_FN_RESOLVE     11
#define UNET_FN_PING        12
#define UNET_FN_RXPAUSE     13
#define UNET_FN_RXRESUME    14
#define UNET_FN_GETINFO     15
#define UNET_FN_LASTERR     16
#define UNET_FN_SETOPT      17
#define UNET_FN_LISTEN      18
#define UNET_FN_UNLISTEN    19

/* ----- error codes (returned in A) ----- */
#define NERR_OK             0
#define NERR_HW             1
#define NERR_NONET          2
#define NERR_DNS            3
#define NERR_CONNECT        4
#define NERR_SEND           5
#define NERR_RECV_TIMEOUT   6
#define NERR_CLOSED         7
#define NERR_CANCEL         8
#define NERR_PARAM          9
#define NERR_NOTSUP         10
#define NERR_STATE          11
#define NERR_TIMEOUT        12
#define NERR_BUSY           13
#define NERR_PROTO          14
#define NERR_AGAIN          15  /* SEND suspended (UNET_OPT_SENDSLICE): repeat the
                                    same unet_send_ch() call to continue */

/* ----- capability bits (unet_caps) ----- */
#define UNET_CAP_TCP         0x0001
#define UNET_CAP_UDP         0x0002
#define UNET_CAP_RESOLVE     0x0004
#define UNET_CAP_PING        0x0008
#define UNET_CAP_MULTICHAN   0x0010
#define UNET_CAP_LISTEN      0x0020
#define UNET_CAP_RAWETH      0x0040
#define UNET_CAP_TRANSPARENT 0x0080
#define UNET_CAP_RXFLOW      0x0100
#define UNET_CAP_ASYNCSEND   0x0200

/* ABI word from GETCAPS (IX): major<<8 | minor. Only the major byte is checked. */
#define UNET_ABI_MAJOR       1

/* ----- STATUS(channel) state bits ----- */
#define UNET_ST_LISTEN       0x0001
#define UNET_ST_CONN         0x0002
#define UNET_ST_RXPEND       0x0004  /* buffered data waiting; only guaranteed with CAP_MULTICHAN */
#define UNET_ST_ACCEPT       0x0008

/* STATUS(0xFF) network-wide bits (DE) */
#define UNET_NETST_CONFIGURED 0x0001
#define UNET_NETST_INIT       0x0002

/* ----- RECV flag bits (IX) ----- */
#define UNET_RXF_TRUNC       0x0001
#define UNET_RXF_MORE        0x0002
#define UNET_RXF_LOST        0x0004  /* overrun/dropped frame since the last RECV */
#define UNET_RXF_XCHAN       0x0008  /* the OTHER channel has data; not "link idle" */

/* ----- GETINFO fields ----- */
#define UNET_IF_BACKEND      0
#define UNET_IF_IP           1
#define UNET_IF_MASK         2
#define UNET_IF_GW           3
#define UNET_IF_MAC          4
#define UNET_IF_DNS1         5
#define UNET_IF_DNS2         6
#define UNET_IF_IPSRC        7
#define UNET_IF_SSID         8
#define UNET_IF_BAUD         9
#define UNET_IF_NTP          10
#define UNET_IF_TZ           11
#define UNET_IF_HW           12

/* ----- SETOPT options ----- */
#define UNET_OPT_CANCELKEYS  1
#define UNET_OPT_RXTRIG      2
#define UNET_OPT_SENDSLICE   3

/*
 * Register block exchanged with a DLL function through the asm trampoline
 * (unetcall.s: unet_raw_call). HL/BC are consumed by the call mechanics and
 * never appear here; every UNET function only sees/returns A/DE/IX/IY.
 */
typedef struct {
    u8  a;
    u16 de;
    u16 ix;
    u16 iy;
} unet_regs;

/* unet_load() error codes (distinct from NERR_*, all negative) */
#define UNET_LOAD_OK         0
#define UNET_LOAD_ENOMEM    -1   /* GETMEM failed (no free DSS page) */
#define UNET_LOAD_EOPEN     -2   /* file not found / open failed */
#define UNET_LOAD_EFORMAT   -3   /* bad header / not an L1 DLL */
#define UNET_LOAD_ETOOBIG   -4   /* relocation bitmap larger than UNET_RELOC_MAX */
#define UNET_LOAD_EINIT     -5   /* DLL INIT hook refused (e.g. wrong window) */
#define UNET_LOAD_EABI      -6   /* GETCAPS ABI major mismatch, or no CAP_TCP */

/*
 * Stage, relocate and initialise an L1 DLL from `path` (a full or bare DSS
 * path — this layer does no search; see unetld.h for NET-driven selection and
 * resolution beside the EXE). Loads into a private DSS page that is mapped
 * into WIN1 (0x4000) only for the duration of each unet_call() — WIN1's
 * contents outside a call are the caller's own code/data, untouched.
 * Returns UNET_LOAD_OK (0) or a negative UNET_LOAD_* code.
 */
i8   unet_load(const char *path);

/* Run FINI (best-effort) and free the DLL's page. Safe to call repeatedly,
 * including when nothing is loaded. */
void unet_free(void);

u8   unet_loaded(void);          /* 1 if a DLL is currently loaded */
const char *unet_dll_name(void); /* cached self-reported name (offset 16 in the
                                     L1 header), NUL-terminated within 16 bytes;
                                     valid only once unet_loaded() */
u16  unet_caps(void);            /* GETCAPS capability bitmask, cached at load */
u16  unet_abi(void);             /* GETCAPS ABI word (major<<8|minor), cached at load */

/*
 * Dispatch UNET function `fn` with *r as both input and output registers.
 * Maps WIN1 to the DLL's page, calls through its export table, restores
 * whatever WIN1 (and WIN3, which the DLL may claim for its ISA card) held
 * before — the caller's own WIN1 code/data is displaced only for the span of
 * this one call. Returns r->a (also written into *r).
 * NERR_STATE (with CF-equivalent left at r->a) if no DLL is loaded.
 */
u8   unet_call(u8 fn, unet_regs *r);

/*
 * Channel-0-only convenience wrappers -- the only ones SprinTalk (net_unet.c)
 * and the loader diagnostic (netdll.c) need. unet.rel links as a plain object
 * file, not a library, so an unused wrapper here still costs image bytes on
 * a build that is tight against the win0 layout's code ceiling (see the port
 * plan's memory-budget risk) -- CAP_MULTICHAN, LISTEN/UNLISTEN, RESOLVE,
 * PING, RXPAUSE/RXRESUME, LASTERR and SETOPT are all real UNET functions
 * (see the UNET_FN_ and UNET_CAP_ constants above) with no caller yet; add a
 * wrapper the moment something needs one, or call unet_call() directly with
 * a one-off unet_regs for something that doesn't warrant its own wrapper.
 */
i8   unet_connect(const char *host, const char *port);
i16  unet_send(const void *buf, u16 len);
i16  unet_recv(void *buf, u16 max, u16 timeout_ms, u16 *flags);
i8   unet_close(void);

i8   unet_netinit(void);         /* UNET_FN_NETINIT */
i8   unet_netdone(void);         /* UNET_FN_NETDONE */
i8   unet_netstatus(u16 *bits);  /* STATUS(0xFF): NERR_OK/NERR_NONET, *bits = UNET_NETST_* */
i8   unet_getinfo(u8 field, char *dst, u16 max);

/* LASTERR: the backend's own account of its last failure, written to dst as an
   ASCIIZ line (empty when no DLL is loaded). UNETRTL reports the ISA slot and
   base, the stage that failed, its NERR_*, the internal TCP/resolver codes and
   the NIC's captured transmit diagnostics; UNETESP reports the tail of the last
   unparsed AT response. Diagnostic only -- never branch on its text. */
i8   unet_lasterr(char *dst, u16 max);

#endif /* UNET_H */
