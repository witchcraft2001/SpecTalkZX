/*
 * unetld.h — UNET backend selector/loader (UNETLD-SPEC.md), on top of unet.h.
 *
 * Reads the DSS `NET` environment variable (published by the backend's own
 * bring-up tool — NETUP for Wi-Fi, NETCFG+IFUP for RTL/3C509B — never set by
 * hand), resolves it to a DLL name, finds the DLL beside the running EXE,
 * loads and validates it, and brings the link up. Mirrors the algorithm in
 * unet_libs_core/docs/UNETLD-SPEC.md exactly (same error codes, same staging:
 * RESET -> SELECT -> LOAD -> NETSTART -> ... -> UNLOAD).
 */
#ifndef UNETLD_H
#define UNETLD_H

#include <sprinter.h>
#include "unet.h"

/* UNETLD_E_* — loader error codes (unet_abi.toml group "unetld_e") */
#define UNETLD_E_NONE      0
#define UNETLD_E_NOENV     1   /* NET not set or empty */
#define UNETLD_E_BADVALUE  2   /* NET not 3-4 chars of [A-Z0-9] */
#define UNETLD_E_LOAD      3   /* unet_load() failed to open/decode the DLL */
#define UNETLD_E_NAME      4   /* DLL's self-reported name doesn't match the tag */
#define UNETLD_E_CALL      5   /* GETCAPS dispatch/status failure */
#define UNETLD_E_ABI       6   /* unsupported ABI major */
#define UNETLD_E_STATUS    7   /* NETSTART: unexpected STATUS(0xFF) result */
#define UNETLD_E_NETINIT   8   /* NETSTART: NETINIT failed */

/* UNETLD_F_* — state flags */
#define UNETLD_F_LOADED    0x01
#define UNETLD_F_NETINIT   0x02

void unetld_reset(void);          /* clear all state; safe to call repeatedly */

/*
 * SELECT: read NET, validate/upper-case it, resolve the WIFI->ESP alias,
 * build the tag and "UNETxxxx.DLL" name. Returns 0, or a negative
 * -UNETLD_E_NOENV/-UNETLD_E_BADVALUE.
 */
i8   unetld_select(void);

/*
 * LOAD: resolve the DLL beside the running EXE (falling back to the bare
 * name / current directory) and unet_load() it, then validate its name and
 * ABI/CAP_TCP. Requires a prior successful SELECT. Returns 0, or a negative
 * -UNETLD_E_LOAD/-UNETLD_E_NAME/-UNETLD_E_CALL/-UNETLD_E_ABI. On any failure
 * from name-check onward the DLL handle may still be open; call
 * unetld_unload() before retrying.
 */
i8   unetld_load(void);

/*
 * NETSTART: STATUS(0xFF) then NETINIT. Requires a prior successful LOAD.
 * Returns 0, or a negative -UNETLD_E_STATUS/-UNETLD_E_NETINIT.
 */
i8   unetld_netstart(void);

/* REQUIRE: pure capability gate, (unet_caps() & mask) == mask. */
u8   unetld_require(u16 mask);

/* UNLOAD: best-effort NETDONE (if NETINIT succeeded) + unet_free(), then
 * clear the state flags. Always "succeeds"; safe to call more than once.
 * The read-only surface below survives it on purpose, so a caller that
 * unloaded after a failure can still report the tag, DLL name and reason;
 * unetld_select() wipes everything at the start of the next bring-up. */
void unetld_unload(void);

/* ----- read-only state surface ----- */
const char *unetld_net_tag(void);   /* resolved tag, e.g. "ESP" */
const char *unetld_dll_name(void);  /* "UNETxxxx.DLL" */
const char *unetld_dll_path(void);  /* full path LOAD tried; "" if it used the bare name */
u8   unetld_error(void);            /* last UNETLD_E_* code */
u8   unetld_last_status(void);      /* last NERR_* from STATUS/NETINIT/GETCAPS */
u8   unetld_flags(void);            /* UNETLD_F_* bits */

#endif /* UNETLD_H */
