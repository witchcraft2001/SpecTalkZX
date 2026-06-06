# SpecTalk Sprinter port — TODO

## Navigation & autocompletion rework (proposed)

Goal: free **Tab** for autocompletion by moving window switching to other keys.

- [ ] Window switching:
        * **Ctrl+Tab** = next window (forward)
        * **Shift+Tab** = previous window (reverse)
      (Tab alone stops switching windows — reserved for completion below.)
- [ ] Quick channel select: **Alt+<digit>**. Number channels from **1**; the
      digit **0 = the 10th** channel (Alt+1..Alt+9, Alt+0). Open question: window
      0 is currently the server window. Decide the mapping — e.g. keep server as a
      separate slot (Alt+`/Esc-to-server?) and number only channels/queries 1..10,
      which means MAX_WIN must hold server + 10 (today MAX_WIN=10 includes server).
      Update the status-bar window list to match the 1-based numbering.
- [ ] **Tab = autocompletion** for two contexts:
        * at the start of the line after `/` → complete **command** names;
        * elsewhere → complete **nicks** from the current channel's member roster
          (requires the per-channel nick list — see below).
      Collision handling (multiple candidates share the typed prefix), suggested:
        1. First Tab completes to the **longest common prefix** of all matches.
        2. If still ambiguous, repeated Tab **cycles** through the candidates
           (insert each in turn); show the candidate list on the notif line.
        3. A nick completion at the start of a line may append a separator
           (e.g. ": ") like common clients; mid-line just inserts the nick.
- [ ] Prerequisite for nick completion: maintain a **per-channel member roster**
      (from RPL_NAMREPLY 353/366 + JOIN/PART/QUIT/KICK/NICK), bounded in size
      (e.g. last ~40 active nicks; store in a DSS page like history if needed).

## Legacy cleanup (de-ZX-ification)

When the original `irc_handlers.c` / `user_cmds.c` / `spectalk.c` logic is adapted
in (Stage 5+), it carries ZX/z88dk-isms that should be progressively cleaned to
idiomatic Sprinter-SDK C. Track and remove:

- [ ] Strip z88dk ABI annotations (`__z88dk_fastcall`, `__z88dk_callee`,
      `__naked`/`ST_NAKED`) from ported code — SDCC uses its own convention.
- [ ] Remove the global parser-context trick (`pkt_usr`/`pkt_par`/`pkt_txt`/
      `pkt_cmd` globals used to avoid stack args) where it only existed to save
      ZX bytes; pass args normally unless profiling says otherwise.
- [ ] Replace ZX rendering hooks (`print_str64`, `print_char64`, `draw_status_bar`,
      `clear_main`, `clear_line`, `notify`/`notify2`, `beep`) with the `term_*` HAL.
- [ ] Replace `uart_send_string`/`uart_send_line`/`uart_send_crlf` with `net_*`.
- [ ] Drop BPE string compression (`SB_*` tokens, `SPECTALK.DAT` dict) — not used
      on Sprinter; use plain string literals (they live in code/rodata).
- [ ] Drop theme-as-ZX-attributes (`themes.h`); map nick/mention colors to DSS
      16-color attributes via `term`.
- [ ] Remove overlay system assumptions (help/about/config/status were SD-loaded
      overlays); make them resident text screens.
- [ ] Re-evaluate buffer aliasing tricks (e.g. `names_friend_buf` over `notif_buf`)
      — keep only if memory in WIN1 is actually tight.
- [ ] Replace `st_stricmp`/`st_stristr`/`u16_to_dec`/etc. ASM utils with C (or SDK
      string lib) versions.
- [ ] Spanish comments: keep as-is when adapting, translate opportunistically.

## Functional TODO

- [ ] On-the-fly UTF-8 <-> CP866 recoding (the world is UTF-8; Sprinter renders
      CP866). A persisted setting (toggle, default ON for public channels):
      - RECEIVE (incoming text UTF-8 -> CP866): decode UTF-8 sequences; map
        Cyrillic U+0400..U+04FF and common punctuation (dashes, quotes, NBSP...)
        to their CP866 byte; ASCII (<0x80) passes through; unmappable -> '?'.
        Apply to the displayable text of PRIVMSG/NOTICE/TOPIC/NAMES; protocol
        tokens (commands, nicks, channels) are ASCII and untouched. Lines are
        assembled whole before recoding, so multi-byte sequences never split.
      - SEND (CP866 input -> UTF-8): expand high bytes (0x80..0xFF) to their
        UTF-8 multi-byte form before transmit; ASCII passes through.
      - Needs a CP866<->Unicode table for 0x80..0xFF (128 entries; the standard
        CP866 codepage). Replaces the original SpecTalk's lossy UTF-8->ASCII
        folding with proper Cyrillic-preserving recoding.
      - Command e.g. /encoding (utf8|cp866|raw); save in SPECTALK.CFG.
      - Best implemented with / after the WIN1+WIN2 code layout (adds ~0.5 KB).


- [x] Persist user settings to SPECTALK.CFG (current dir): last server/port/nick.
      On startup: set the nick, seed the input history with `/server <last> <port>`
      (Up recalls it), and show a hint. Saved on /server and /nick.
      TODO extend: nickpass (done), autoconnect, theme, toggles (original config
      keys); option to store in %NET_DIR%; fall back gracefully if read-only.
- [x] Multi-server history: SPTALK.CFG keeps the last CFG_MAX_SRV servers
      (SRV1=..SRV5=, newest first, deduped). On startup all are seeded into the
      input recall, so Up cycles through them (newest first). Saved on /server.
      Optional later: a small picker, per-server autojoin channels.
- [~] BUG (fix applied, needs HW verification): after SprinTalk exited, kit tools
      (ftp/wget) reported "ESP communication error #1". ROOT CAUSE: the kit tools
      run the ESP in NORMAL mode (CIPMODE=0: AT+CIPSEND=<len> + "SEND OK", +IPD
      receive); SprinTalk used transparent mode (CIPMODE=1) and never restored
      CIPMODE=0 on exit, so their AT commands failed. FIX: net_close() now calls
      esp_restore() which escapes transparent (+++), CIPCLOSE, and AT+CIPMODE=0,
      and it runs on EVERY exit (incl. after a timeout/CLOSED), guarded by
      ever_used. Echo left ATE0 (the kit uses ATE0 too, e.g. tcptest.asm).
      Verify on HW: run SprinTalk, /quit or ESC, then run wget/ftp — should work.
      If it still fails, check baud (UART_DEF persisted?) and whether the kit's
      hardware ESP_RESET (MCR OUT1) is enough on its own.
- [ ] /register <password> <email>: convenience wrapper that sends
      `PRIVMSG NickServ :REGISTER <password> <email>` (one-time nick registration),
      so the user doesn't have to type it through /msg. Maybe also /verify.
- [ ] Per-window paged history (NEXT): a DSS page per window (Dss.GetMem, up to
      16 KB), append all messages, restore last lines on switch, PgUp/PgDn to
      scroll. Uses the WIN3-map + copy-to-WIN2-before-DSS discipline.
- [ ] Stage 5b: multi-window/channel model (up to 10), window switching, activity
      indicators, mention highlight.
- [ ] Stage 6: timestamps, nick coloring, notifications, friends, ignore, NickServ
      auto-id, away, help/about/config/status screens.
- [ ] net layer: detect ESP errors (CLOSED/ERROR) in net_connect instead of fixed
      delays; surface connect failures; reconnect.
- [ ] Hardware scroll via BIOS #8A (O(1)) to replace the RAM-ring chat redraw.
- [ ] CODE LIMIT: code is in WIN1 only (crt0_page2 puts data+stack in the
      allocated WIN2), so code caps at ~15.5 KB. When code approaches it, switch
      to the SDK "Default Layout" (32 KB WIN1+WIN2): code 0x4100 spanning into
      WIN2, data+stack high. Needs (a) a custom crt0 that zeroes _DATA but does
      NOT GETMEM (default crt0 only clears _BSS, and our globals live in _DATA),
      and (b) padding the loaded image to >=16 KB so DSS maps 2 pages (the WIN2
      page is only "owned" if the image reaches it). Deferred until needed.
      Beyond 32 KB: overlays / dss_getmem_pages + setwin_page, or --codeseg banking.
- [ ] Stage 7: NE2000 backend behind net.h; packaging + docs.
