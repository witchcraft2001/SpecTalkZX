# SpecTalk ZX → Sprinter DSS Port — Plan

## Goal

Port SpecTalk ZX (the ZX Spectrum IRC client in this repo) to the **Sprinter**
computer running **DSS**, native to its architecture:

- **80×32 text mode** console (DSS video mode `#03`), replacing the bespoke
  64-column pixel-font renderer.
- A **universal network core** with swappable backends, defaulting to
  **SprinterWiFi (ESP)** but recompilable for an **ISA NE2000-compatible
  (RTL8019A)** card later, with no changes to the IRC/UI layers.
- Connection parameters (Wi-Fi SSID/password, baud, static IP/DNS) read from
  **`net.cfg`** located in the directory named by the **`%NET_DIR%`**
  environment variable.

The portable value of this project is the IRC protocol engine and the
multi-window UI/state model. Everything below the line — rendering, input,
UART, file I/O, timing, memory layout, the build — is ZX-Spectrum/z88dk specific
and gets replaced with a Sprinter HAL built on the SDCC Sprinter SDK.

## Target platform (verified)

- **Toolchain:** SDCC 4.5.0 Sprinter SDK at
  `/Users/dmitry/dev/zx/sprinter/sdcc45-sprinter-sdk` (preferred — best DSS
  library coverage, simple one-pass build, C99). Fallback: SDCC 2.9.0 SDK at
  `…/sdcc-sprinter-sdk`. `z88dk` at `/Users/dmitry/dev/zx/z88dk` exists but is
  **not** used for the Sprinter target.
- **Build pattern** (`sdcc45-sprinter-sdk/examples/common.mk`):
  `sdcc -mz80 --opt-code-speed -c` per source → link with `--no-std-crt0
  --code-loc 0x4100 <crt0.rel> <objs> -l sprinter.lib` → `tools/ihx2exe.py`
  produces the **`.EXE`** (512-byte DSS header, `load=entry=0x4100`,
  `stack=0xBFFF`).
- **DSS C API** (`sdcc45-sprinter-sdk/include/sprinter/{dss,video,types,ports}.h`):
  - Text: `dss_setvmod(0x03, page)` for 80×32, `dss_putchar`, `dss_puts`,
    `dss_gotoxy(x,y)`, `dss_clrscr`, plus DSS `LOCATE/CLEAR/SCROLL/PCHARS`
    primitives for windowed drawing and scroll-region control.
  - Keyboard: `dss_scankey(&key)` (non-blocking — required for the main poll
    loop), `dss_waitkey`, modifier/lock state via the `dss_key_t` struct.
  - Files: `dss_open/creat/read/write/seek/close`, `dss_ffirst/fnext`.
  - Environment: `dss_getenv("NET_DIR", buf)` / `dss_setenv`.
  - Memory: `dss_getmem_pages`, `dss_setwin*`, `dss_freemem` (16 KB pages).
  - Time: DSS `SYSTIME` (date/time) for the clock and message timestamps;
    VSync (~50 Hz) for cadence/timeouts.
- **Network hardware:**
  - **ESP (SprinterWiFi):** TL16C550 UART memory-mapped at **`0xC3E8`** (LSR
    `0xC3ED`, MCR `0xC3EC`, IER `0xC3E9`) — i.e. **inside WIN3**
    (`0xC000–0xFFFF`); reachable only while the ISA window is open. See the
    **ISA / WIN3 discipline** below — open ISA only for the access burst and
    close it before any DSS/BIOS call. The ESP firmware provides TCP/UDP via the
    **ESP-AT** command set
    (`AT+CIPSTART="TCP",host,port`, `AT+CIPSEND`, async `+IPD,<len>:` receive).
    Reference driver (sjasmplus): `…/sprinter_wifi/network/src/lib/{isa,esplib,esp_tcp}.asm`.
  - **NE2000 (RTL8019A):** ISA card, register set memory-mapped after
    `ISA.ISA_OPEN`, default I/O base `0x300`. **No on-chip TCP/IP** — needs a
    software ARP/IPv4/TCP stack. Reference (sjasmplus):
    `…/sprinter-rtl8019a/src/lib/{rtl8019,arp_lib,tcp_lib}.asm` and
    `…/sprinter-rtl8019a/sprinter_rtl8019_soft.md`.
  - The Sprinter network libs build to standalone `.EXE`s with **sjasmplus** and
    are **not SDCC-linkable** — treat them as authoritative reference for ports,
    register maps, and AT sequences, not as a library to link.

### ISA / WIN3 discipline (mandatory)

Both cards (ESP UART at `0xC3E8`, NE2000 registers) are memory-mapped into
**WIN3** (`0xC000–0xFFFF`). `ISA.ISA_OPEN`
(`…/sprinter_wifi/network/src/lib/isa.asm`) **saves the current WIN3 page,
remaps WIN3 to the ISA space, and puts the system port in a special mode**;
`ISA.ISA_CLOSE` restores both. DSS and BIOS calls *also* use WIN3.

Therefore: **open the ISA window only immediately around a card access burst,
and close it before doing anything else.** It is invalid to hold ISA open while
calling DSS/BIOS (text output, keyboard, file I/O, `getenv`, memory, time) or any
code that touches WIN3 — doing so reads/writes ISA ports instead of RAM and
corrupts state. Concretely:

- Wrap every register/data-port burst in `isa_open()` … `isa_close()`. Keep the
  bracket as short as possible; never span the main loop or a DSS call with it.
- All RX/TX buffers, AT command/response strings, and parser state must live in
  **normal RAM in WIN1/WIN2** (outside WIN3), never in the ISA window. The
  pattern is: open → copy bytes between the UART/card and a RAM buffer → close →
  parse/print with ISA closed.
- Re-entrancy/interrupts: ensure no ISR or DSS callback that touches WIN3 runs
  while ISA is open (mask or keep the bracket atomic).

## Reuse vs. replace

| Layer | Current (ZX) | Sprinter port |
|---|---|---|
| IRC parsing/dispatch | `src/irc_handlers.c` | **Keep** (platform-free C; strip ZX assumptions) |
| User commands `/…` `!…` | `src/user_cmds.c` | **Keep** |
| Channel/window state, command history, friends/ignores | `src/spectalk.c` (logic parts) | **Keep** the model; rewrite the I/O/render parts |
| Main loop | `src/spectalk.c` | **Rewrite** around `dss_scankey` + non-blocking `net_recv` poll |
| Screen rendering (64-col font, attrs, themes, badges, Ikkle font, overlays) | `asm/spectalk_asm.asm`, `include/themes.h`, `overlay/*` | **Replace** with DSS 80×32 text calls; overlays become plain resident screens |
| Keyboard | `asm/spectalk_asm.asm` | **Replace** with `dss_scankey` |
| UART | `asm/divmmc_uart.asm` (divMMC 115200) | **Replace** with TL16C550 `0xC3E8` driver |
| File I/O | esxDOS path in `spectalk.c`/ASM | **Replace** with DSS file API |
| Overlay loader, ring-buffer overlay exec | `asm/overlay_loader.asm` | **Drop** (Sprinter RAM is ample; keep screens resident) |
| BPE string compression | `tools/bpe_*`, `SPECTALK.DAT` | **Drop** (memory not constrained the same way) |
| Memory map / BSS trim / `0xF500` | Makefile + crt | **Drop**; use SDK crt0 + `--code-loc` |
| z88dk copt rules, `__z88dk_*` ABI | `src/spectalk_copt.rul`, decls | **Drop**; SDCC ABI (returns in DE), inline `__asm` where needed |
| Build | z88dk Makefile (unity build) | **Replace** with SDCC SDK Makefile (`common.mk` pattern) |

Net effect: the platform replacement is large but *simplifying* — the hardest ZX
code (pixel font renderer, attribute math, overlay paging, BSS tricks, BPE) all
disappears behind the DSS text and file APIs.

## Universal network core

Single C header that both the IRC engine and the backends agree on; backend
chosen at **compile time** via a `NET_BACKEND` define so the IRC/UI layers never
change.

```c
/* net.h — platform/card-agnostic transport */
int8_t   net_init(void);                              /* probe HW, read net.cfg, bring link up; <0 = error */
int8_t   net_connect(const char *host, uint16_t port);/* >=0 conn id, <0 error */
int16_t  net_send(const uint8_t *buf, uint16_t len);  /* bytes sent, <0 error */
int16_t  net_recv(uint8_t *buf, uint16_t maxlen);     /* >0 bytes, 0 none (non-blocking), <0 error */
void     net_close(void);
uint8_t  net_status(void);                            /* NO_HW / LINK_DOWN / LINK_UP / CONNECTED */
```

- **ESP backend (`net_esp.c`, default):** drive the TL16C550 at `0xC3E8` under
  the **ISA / WIN3 discipline** above — each UART burst is `isa_open()` → poll
  LSR / move bytes to-or-from a RAM buffer in WIN1/WIN2 → `isa_close()`, and all
  AT command/response parsing happens with ISA closed. Reimplement the minimal AT
  exchange in C (`AT`/`ATE0`/`AT+CWMODE`/`AT+CWJAP` join from net.cfg,
  `AT+CIPSTART`, `AT+CIPSEND` framed send, `+IPD` async parse into a ring buffer
  for `net_recv`). The original `divmmc_uart.asm` + the ZX UART logic is the
  model; the AT strings come from `esp_tcp.asm`. This mirrors what SpecTalk
  already does on ZX (ESP8266 over UART), so the IRC layer's existing assumptions
  mostly hold — the new wrinkle vs. ZX is the open/close bracketing, since on ZX
  the UART was directly addressable and here it shares WIN3 with DSS.
- **NE2000 backend (`net_ne2000.c`, phase 2):** software stack. Either port the
  RTL8019A sjasm stack’s logic to C (ARP → IPv4 → single-socket TCP, DHCP
  optional) or wrap a minimal uIP-style stack. Heavier; gated behind the same
  `net.h` so it can land later without touching IRC code.
- **`net.cfg` loader (`netcfg.c`):** `dss_getenv("NET_DIR", dir)` → open
  `<dir>\NET.CFG` → parse INI keys `SSID, PASS, DHCP, IP, GATEWAY, NETMASK,
  DNS1, DNS2, NTP, TZ, BAUD`. ESP backend consumes SSID/PASS/BAUD (+ static IP if
  DHCP=0); NE2000 backend consumes IP/GATEWAY/NETMASK/DNS/DHCP. Format and field
  names match the existing kit (`…/sprinter_wifi/network/docs/NETCFG.TXT`) so a
  user's existing `NET.CFG` works unchanged.

## Phased implementation

1. **Skeleton & toolchain.** New `sprinter/` build under the SDCC 4.5 SDK
   `common.mk` pattern. A "hello" `.EXE` that sets 80×32 mode, prints, polls
   `dss_scankey`, exits cleanly. Confirms toolchain + emulator/hardware loop.
2. **Platform HAL.** Implement `term.c` (clear/locate/print/scroll-region for the
   64→80 col layout, status/info/input rows), `kbd.c` (`dss_scankey` →
   the key codes the IRC layer expects, incl. history/word-nav keys), `clock.c`
   (SYSTIME + tick). Define the line/region layout for 80×32 (more columns and
   rows than the ZX 64×17 — rework `SCREEN_COLS`, `MAIN_LINES`, input rows).
3. **net.cfg + ESP backend.** `netcfg.c` + `net_esp.c` reaching CONNECTED to a
   known IRC server; verify raw send/recv with a throwaway main before wiring IRC.
4. **IRC engine integration.** Bring `irc_handlers.c` / `user_cmds.c` and the
   channel/window model across; replace all rendering/input/uart/file calls with
   HAL + `net.h`. Strip themes/badges/Ikkle/overlay code paths or reduce to
   plain text. Get JOIN/PRIVMSG/multi-window working end to end.
5. **Feature parity pass.** Timestamps, nick coloring (map to DSS 16-color
   attributes), notifications, friends/ignore, NickServ, away, command history,
   help/about/config/status as resident text screens.
6. **NE2000 backend (optional/later).** Implement `net_ne2000.c` software stack
   behind `net.h`; add `NET_BACKEND=ne2000` build variant. No IRC-layer changes.
7. **Packaging.** Produce `SPECTALK.EXE`; document `NET.CFG`/`%NET_DIR%` setup;
   release image.

## Build system

Model after `sdcc45-sprinter-sdk/examples/common.mk`. A `sprinter/Makefile`:

```
APP   = SPECTALK
SRCS  = main.c term.c kbd.c clock.c netcfg.c net_esp.c \
        irc_handlers.c user_cmds.c spectalk_core.c
NET_BACKEND ?= esp        # esp | ne2000
include $(SDK)/examples/common.mk
```

Decisions:
- **Drop the unity build** (it existed for z88dk cross-module optimization); use
  SDCC separate compilation per the SDK pattern. If size/perf needs it later, a
  unity `main_build.c` still compiles under SDCC.
- **`CODE_LOC`/`STACK`:** start with SDK defaults (`0x4100` / `0xBFFF`, 32 KB
  WIN1+WIN2). Use `dss_getmem_pages` for large buffers (RX ring, scrollback)
  rather than static BSS if 32 KB proves tight.
- Backend selection via `-DNET_BACKEND_ESP` / `-DNET_BACKEND_NE2000` and
  conditional source inclusion.

## Risks & open questions

- **SDCC ABI vs. legacy asm.** SDCC 4.5 returns values in **DE** (not z88dk's L).
  The existing `asm/*.asm` uses z88dk conventions and is mostly discarded; any new
  asm should be inline `__asm` in C or follow SDCC's convention. Confirm by
  reading the SDK `crt0` and an example with inline asm before writing HAL asm.
- **Code-window size.** 32 KB (WIN1+WIN2) for a fairly large C program plus
  buffers may be tight; plan to move scrollback/RX buffers into `getmem` pages,
  or evaluate a larger layout. Measure early (phase 1–2).
- **ISA / WIN3 conflict (highest-impact constraint).** The card and the UART
  share WIN3 with DSS/BIOS, so ISA must be opened only for short access bursts
  and closed before any DSS/BIOS/WIN3 use (see *ISA / WIN3 discipline*). This
  dictates the backend structure: copy through RAM buffers, never call DSS with
  ISA open, keep brackets atomic w.r.t. interrupts. Get this wrong and failures
  are intermittent and hard to debug. Build the `isa_open/close` bracket and a
  buffered-burst helper first, before any higher-level networking.
- **ESP AT state machine in C.** Async `+IPD` interleaved with command responses
  is the fiddly part; port the framing carefully from `esp_tcp.asm`. Decide
  whether to use `AT+CIPMUX=0` (single connection — sufficient for IRC) vs multi.
- **Does the ESP need joining by us, or is it pre-joined by `NETUP`?** The kit's
  `NETUP`/`NETCFG` may already bring Wi-Fi up; SpecTalk may only need to open a
  TCP connection. Decide whether `net_init` performs `AT+CWJAP` or assumes the
  link is already up from net.cfg + NETUP. (Check `…/sprinter_wifi/network/src/apps/netup.asm`.)
- **80×32 layout redesign.** The UI was tuned for 64×17; rework banner, status,
  info, and input regions for 80 cols × 32 rows (more scrollback, wider input).
- **Character set / UTF-8.** Keep the existing UTF-8→ASCII folding; map to the
  Sprinter font/codepage.

## Reference index

- DSS/BIOS/EXE/text-mode/memory docs:
  `/Users/dmitry/dev/zx/sprinter/sprinter_ai_doc/manual/{03_bios,04_dss,05_graphics,02_memory}/…`
- SDCC 4.5 SDK (preferred): `/Users/dmitry/dev/zx/sprinter/sdcc45-sprinter-sdk`
  (`include/sprinter/*.h`, `examples/common.mk`, `tools/ihx2exe.py`,
  `examples/{01_hello,05_keyinput,07_fileio,06_printf}`)
- SDCC 2.9 SDK (fallback): `/Users/dmitry/dev/zx/sprinter/sdcc-sprinter-sdk`
- ESP/SprinterWiFi driver + AT sequences + NET.CFG:
  `/Users/dmitry/dev/zx/sprinter/sprinter_wifi/network/src/lib/{isa,esplib,esp_tcp,netcfg_lib}.asm`,
  `…/network/docs/NETCFG.TXT`, `…/network/src/apps/{netup,tcptest}.asm`
- RTL8019A/NE2000 driver + software stack:
  `/Users/dmitry/dev/zx/sprinter/sprinter-rtl8019a/src/lib/{rtl8019,arp_lib,tcp_lib}.asm`,
  `…/sprinter_rtl8019_soft.md`
- Example Sprinter C apps for build/idiom reference: `…/sprinter/sources/nupogodi`,
  `…/sprinter/gfxview`, `…/sprinter/flexnavigator`, `…/sprinter/games/titd/src`,
  `…/sprinter/sources/sprinter-unzip`
