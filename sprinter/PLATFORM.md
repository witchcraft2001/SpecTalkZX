# SprinTalk — Sprinter platform notes (developer)

Port of the ZX Spectrum IRC client **SpecTalk ZX** to the Sprinter computer
(DSS), built with the SDCC 4.5 Sprinter SDK. See `../plan.md` for the full plan
and `../CLAUDE.md` for context. Work proceeds in verifiable stages; this
directory is the Sprinter build, independent of the ZX `src/`/`asm/` tree.

(User-facing docs live in `README.md` and `HOWTO.md`; this file is the hard-won
platform/HAL reference.)

## Build

```sh
make                      # -> SPTALK.EXE (ESP backend, the shipping build)
make NET_BACKEND=unet     # -> SPTALK.EXE over UNETxxxx.DLL instead
make deploy               # build + write FAT12 floppy to distr/sptalk.img
make diag                 # optional: NETDIAG.EXE / NETIRC.EXE bring-up tools
make netdll               # optional: NETDLL.EXE, the UNET loader testbed
make clean
```

Object files live in `build/$(NET_BACKEND)/`: `NET_BACKEND` reaches the shared
C sources through `-DNET_BACKEND_UNET`, so the two backends must not share a
build directory.

`make netdll` pairs with `node tools/test_netdll_win0.js`, which runs the real
EXE through sprinter-rtl8019a's Z80/DSS harness against a real
`UNETxxxx.DLL` — the automated check that the win0 DLL mechanics still work.
`make deploy NET_BACKEND=unet` also puts `NETDLL.EXE` on the floppy: it re-runs
SELECT/LOAD/GETCAPS/ABI/NETSTART/GETINFO on the target and names the stage that
failed, which is the only way to tell a wrong `NET` value from a missing DLL.
`node tools/test_sptalk_unet.js` drives the shipping `SPTALK.EXE` itself through
the same harness (SELECT, LOAD, the `CAP_TCP` gate, `NETSTART` against the
modelled RTL8019, clean double-ESC exit). Run it after any change to the DLL
path: `NETDLL.EXE` stops after GETCAPS and its whole payload fits in WIN0, so it
can neither exercise the capability gate nor a DLL swap against a WIN1 half full
of program code, which is exactly how a broken `unetld_require` shipped once.

Override the SDK path if needed: `make SDK=/path/to/sdcc45-sprinter-sdk`.
The build never writes into the SDK tree; the floppy image lives in `distr/`.

## Verify in MAME

```sh
./run.sh        # make deploy + launch MAME with distr/sptalk.img as flop2
```

At the DSS prompt, switch to the floppy drive and run `SPTALK`.

## Verified platform facts (empirical, on MAME v3.06 / DSS)

- DSS launches programs already in **80x32 text mode** — no `dss_setvmod` needed.
- `dss_gotoxy(x, y)` is **1-based** (cols 1..80, rows 1..32).
- **Keyboard:** `dss_scankey` (#31) returns a *raw key code* in `.ascii`, not ASCII.
  For real ASCII use `dss_testkey` (#37) to peek (non-blocking, doesn't consume)
  then `dss_scankey` to pop. `dss_kbhit` maps to CTRLKEY/#33 (modifier state),
  **not** buffer state — don't use it for "key available".
- Key codes (`.ascii`): **ENTER = 0x0D, BACKSPACE = 0x08, ESC = 0x1B, TAB = 0x09**.
- Navigation keys carry no ASCII — identify them by `.scan`:
  **← 0x54, → 0x56, ↑ 0x58, ↓ 0x52, Home 0x57, End 0x51, PgUp 0x59, PgDn 0x53, Tab 0x0F**.
- **`dss_clear` / `dss_scroll` are 0-based**, but `dss_gotoxy` is **1-based**.
  Pass `x-1, y-1` to clear/scroll to align with gotoxy coordinates.
- `dss_clear(x, y, w, h, attr, fill)` sets both SYM (=fill char) and ATTR for the
  region; text printed afterward keeps the region's ATTR (color). This is what
  makes per-nick colouring work: pre-set the nick cells' attribute, then `dss_puts`
  drops the characters on top without touching attributes.
- **Memory / win0 (WIN0+WIN1+WIN2) layout:** SPTALK.EXE is a two-stage PRELOAD
  EXE (SDK's `lib/win0/*`, `tools/win0_exe.py`, ~47 KB budget), not a normal
  one-shot load. DSS loads only a small stage-1 loader at `0x8100`, which
  GETMEMs three private pages and streams the real program in: code+rodata
  span **WIN0** (`0x0180..0x3FFF`) and spill into **WIN1** (`0x4000..0x7FFF`)
  as needed; `_WINRT` (RST `#08`/`#10`/`#38` trampolines, `win0_rt.s`, never
  repaged), `_HIGH` (unet backend only — the DLL call dispatcher, which must
  survive the WIN1 DLL swap below) and `_DATA`+stack live in the private WIN2
  page (`0x8000..0xBF00`). This replaced an earlier flat WIN1+WIN2 layout
  (`crt0_flat`, now deleted) that left only ~89 B of code headroom and no room
  for UNET.DLL support. `tools/check_win0_layout.py` guards the invariants at
  build time (run against the *payload* link, before `win0_exe.py` packs it
  with the loader). The small diagnostics (NETDIAG/NETIRC) still use the SDK
  `crt0_page2` (single allocated WIN2 page, `--data-loc 0x8000`, stack
  `0xBFFF`) — they fit one window and don't need the extra layout.
  - **`_DATA` is not zero-initialized by `crt0_win0.s` itself** — only
    `_INITIALIZER→_INITIALIZED` and `_BSS` are handled there; SDCC 4.5's z80
    backend puts *every* plain uninitialized global into `_DATA` (not `_BSS`).
    `gsinit_zero_data.s` supplies the missing `_GSINIT` fragment (linked right
    after `crt0_win0.rel`) so this still works exactly like the old
    `crt0_flat`'s gsinit did.
  - **A DLL is swapped into WIN1 with a raw `OUT`, not `dss_setwin`.** Under
    win0 every RST `#08`/`#10` call is intercepted by a trampoline that
    restores `WIN1 = _wrt_p1` on return — a plain `dss_setwin(1, dll_page)`
    would be undone by the very RST `#10` call that performs it. The unet
    dispatcher (`unetcore.s`'s `_unet_call`) instead repoints `_wrt_p1` at the
    DLL's page for the span of the call, so a trampoline firing mid-call
    (a DSS call from inside the DLL, or a timer interrupt) restores WIN1 back
    to the DLL rather than evicting it. `_unet_call` itself must live in
    `_HIGH` (WIN2) — under win0 `_CODE` spans WIN0+WIN1, and code executing
    from the WIN1 half would vanish under its own hand the moment it remaps
    WIN1 to the DLL.
  - **The application directory comes from `P0:0x0100`, not `DSS_APPINFO`.**
    The win0 stage-1 loader stages it there (parsed from the PSP before
    handing off) with an explicit warning against calling `DSS_APPINFO`
    directly: it scans the PSP path, which win0's own two-stage PSP layout
    differs from, and can hang the machine when the program was itself
    `dss_exec`'d by another. `unetldcore.s`'s DLL path resolution reads
    `0x0100` (no trailing separator) instead.
  - **`net_close()` closes the socket, never the link layer.** `NETDONE` tears
    the DLL's network state down for good: every later `CONNECT` answers
    `NERR_STATE`. Since `net_close()` is also `/close` and the keepalive
    timeout's teardown, calling `NETDONE` there cost the session its ability to
    reconnect. `NETDONE` + `FINI` belong to program exit only (`unetld_unload`).
  - **A failing SEND costs a full TCP retransmit timeout inside the DLL**, so
    the retry ladder has to be short. Only attempts that move zero bytes spend
    the budget, a stalled link short-circuits the next line in the same batch,
    and the exit path skips its `QUIT` once a send has given up. Before that,
    one wedged link meant minutes of frozen UI, twice (once live, once on exit).
  - **`unetld_require()` is `(caps & mask) == mask`, not `== caps`.** Every
    UNET DLL reports more than TCP (UNETRTL adds UDP and raw), so a gate that
    compares the masked result against the *caps* byte rejects all of them.
    That bug made `net_init()` return `NET_NO_HW`, which the UI then reported
    as a missing ESP card.
  - **A real IRC server talks first, and that is a distinct backend code
    path.** Every server pushes its `NOTICE AUTH` banner the moment the
    handshake closes and keeps talking while the client is still sending
    `NICK`/`USER`, so the client's first SEND waits for its ACK with
    unsolicited peer data — carrying a *lower* ack number — arriving ahead of
    it. UNETRTL 0.3.0 answered that with `NERR_SEND` ("no ACK from peer") and
    the registration never completed; 0.3.1 (`fix(unetrtl): preserve TCP
    payload before ACK`) handles it. Pin the backend revision: the shipped
    DLLs are a vendor drop in `unet_libs_core/dll` with a `manifest.json`, and
    a request/response test peer will not notice a backend that gets this
    wrong. `tools/test_sptalk_unet.js` now models a greeting server for
    exactly this reason, and the bring-up banner prints the DLL's own name
    field (`UNETRTL v0.3.1`) so a field log says which revision ran.
  - **Never hand a UNET DLL a pointer into WIN0.** The ABI permits it, but
    UNETRTL maps its own "cold" overlay page over WIN0 (`lib/win0cold.asm`) for
    the ARP/DNS/PING frame builders, so a caller buffer there is paged out
    mid-call. Under the win0 layout the command line at `P0:0x0080` is the easy
    mistake: a literal dotted quad passed straight from there fails to parse and
    silently becomes a DNS lookup. `net_unet.s` already stages host, port and
    every outgoing line into `_DATA` (WIN2); `netdll.c` now copies its arguments
    the same way, and `tools/test_netdll_win0.js` fails if a DNS query appears.
  - **WIN3 is saved and restored around every DLL call.** The UNET ABI lets a
    DLL map its own ISA card into WIN3 per call and does not promise to put
    the caller's page back, so `_unet_call` brackets each dispatch with
    `in a,(#0xE2)` / `out (#0xE2),a` just as it does for WIN1.
- **A DSS memory block ID is NOT a physical page number.** `GETMEM` (#3D)
  returns a *block ID* in 1..255 (BIOS `EMM_FN2`); only `SETWIN`
  (#38/#39-#3B, block + page-index) understands one. The window ports
  `#82`/`#A2`/`#C2`/`#E2` take *physical page* numbers, and a block's pages
  need not even be contiguous. To map a block by raw `OUT` later, cache its
  page first — either `SETWIN` it once and read the port back (`in a,(#0xE2)`,
  what `unetcore.s`'s `unet_load` and `lib/win0/loader.c` both do) or ask BIOS
  `EMM_FN4`/`EMM_FN5`. libman's own `_L_CALL` follows the same rule. Confusing
  the two maps a page the program never owned; because 0 is never a valid
  block ID, it also makes 0 a safe "nothing allocated" sentinel.
- **Printing: use `dss_puts` (PCHARS #5C), not per-char `dss_putchar`** — one
  syscall per line vs one per character (~80× fewer syscalls; per-char redraw of
  the chat was the cause of multi-second startup).
- **ESP bring-up model (verified on real HW):** the SprinterWiFi kit's `NETUP`
  utility joins Wi-Fi as a one-time step and publishes the live network state in
  DSS environment variables (`NET`, `NET_BAUD`, `NET_IP`, …). Apps do NOT re-join —
  they read `NET`/`NET_BAUD`, init the local UART at that baud, and open TCP via
  `AT+CIPSTART` (transparent mode CIPMUX=0/CIPMODE=1), assuming the link is up.
  SPTALK refuses to run if `NET != WIFI` ("run NETUP first"). The TL16C550 UART
  probe returns PRESENT on the test machine.
- **Disconnect detection:** in transparent mode the ESP prints `\r\nCLOSED\r\n` on
  the UART when the socket closes; `net_poll` watches the RX stream for that token.
- **ISA slot is NOT fixed to ISA1.** `isa_open` maps WIN3 to slot byte
  `(slot<<1)|0xD4` → slot0=0xD4 (ISA1), slot1=0xD6 (ISA2). NETUP publishes the
  detected slot in env var `NET_ESP_HW` = `"<slot>/#3E8"` (first char = slot 0/1);
  `net_init` reads it and calls `isa_set_slot` before probing. `uart_probe` also
  scans both slots (and checks the IER hi-nibble reads 0, like the network lib's
  `UART_FIND`) as a fallback. Hardcoding 0xD4 = "ESP not found" when the card is in
  the other slot.
- **RX data loss: lower the FIFO trigger; do NOT toggle RTS by hand.** NETUP enables
  full hardware flow control on the ESP at setup — confirmed on HW: its
  `+UART_CUR:115273,8,1,0,3` line shows flow=3 even at the default baud (trailing `3`).
  So the ESP honours RTS/CTS and an app must NOT re-send `AT+UART_CUR` (needless
  baud-touch risk) nor turn flow off on exit. The loss came from the local 16550's
  AFE trigger being **8**: AFE deasserts RTS only once 8 bytes sit in the FIFO,
  leaving just 8 bytes of in-flight headroom — a slow-reacting ESP keeps sending past
  that during a long render (scroll + per-row draw) and overruns (lost MOTD chunks).
  Fix: lower the **RX trigger** so AFE drops RTS earlier. Now AFE drops RTS as soon as
  the FIFO reaches the trigger, so while net_poll isn't draining (during the render)
  RTS stays low and the ESP is held off the *whole* time — the same "pause during
  render" effect, but driven automatically by the FIFO level. **Do not** force RTS low
  manually via `MCR=0x20`: on real HW that wedged RX completely (RTS apparently stuck
  low / never resumed). Let AFE drive RTS off the FIFO — that is the mechanism that
  already worked at trigger 8, just safer. Tuning (verified on HW): trigger **1**
  (`FCR=0x07`) eliminated the loss but throttled bursts (MOTD slow); trigger **4**
  (`FCR=0x47`, 12 bytes headroom) is the chosen speed/safety compromise. Trigger 8
  (`FCR=0x81`) was the original lossy setting.
- **Scrolling: DSS `#55` (SCROLL) wedges** on a full-width region (it calls BIOS
  `#B7` internally; see `sprinter_dss/VIDEO.ASM`). The working approach (used by
  `texteditor`) is **BIOS `#8A` (LP_SCROLL_UD) directly**: `B`=dir (1=up, 2=down),
  `D`=top row (0-based), `E`=row count, wrap in `di`/`ei`. SPTALK uses this for the
  chat region.
- **Cyrillic = CP866** (lowercase 'е' = 0xA5); the high range 0x80–0xFF is
  printable and renders Cyrillic glyphs directly. Incoming UTF-8 is recoded to
  CP866 and outgoing CP866 to UTF-8 (toggle with `/encoding`).
- **SDCC 4.5 miscompiles `if (x != g) { g = x; ... }` for `u8` globals.** It
  emits `ld a,x / ld hl,#g / sub a,(hl) / jr Z,skip / ld (g),a` — the store
  believes `A` still holds `x`, but `SUB` left the *difference* there, so `g`
  takes a wrong value and the guard reports a change on every call afterwards.
  Nothing in the program's output looks wrong; it just does the guarded work
  forever. This hit both of SprinTalk's change guards (`term_clock`'s second
  counter and `irc_net_warn`'s flag) and cost roughly 8× the main loop's
  throughput. **Write before comparing**:
  `prev = g; g = x; if (prev != x) { ... }` — then the store uses a value that
  is provably live. Grep a build for `sub[ \t]+a, \(hl\)` followed within a
  few lines by `ld\t(_`; `tools/test_sptalk_unet.js` also bounds the idle
  loop's screen writes per pass, which is what catches a relapse.
- **Cost of one main-loop pass is the responsiveness budget.** DSS buffers
  keystrokes in an interrupt handler, but a UNET DLL runs with interrupts off
  while it owns the ISA window, so keys are only safe if the loop comes back
  around quickly and takes *all* buffered keys, not one. SPTALK drains up to
  eight per pass and repaints the input row once afterwards; one key per pass
  cost a full 80-column row repaint per character typed.

## Stages

1. **HAL smoke test** (done) — 80x32 text, border, clock, keyboard, clean exit.
2. **Terminal HAL + chat layout** (done) — banner/chat/status/input, `dss_puts`
   redraw, input editor (cursor move, history), colours.
3. **net.cfg loader + ISA/UART diagnostics** (done) — `%NET_DIR%`/NET.CFG, ISA
   open/close discipline, TL16C550 probe. → `NETDIAG.EXE`. Verified on real HW.
4. **ESP TCP + IRC handshake** (done) — UART driver, ESP transparent mode, TCP to
   irc.libera.chat, NICK/USER, auto-PONG, session cleanup. → `NETIRC.EXE`.
5. **IRC engine + UI integration** (done) — windows, parser/dispatch, history.
6. **Feature parity** (in progress) — private msgs (`/query` `/msg`), `/me`,
   NickServ identify, nick colouring, env-var network config, recoding.
7. **NE2000 backend + packaging** (RTL build) — pending.
