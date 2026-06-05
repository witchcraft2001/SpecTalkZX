# SpecTalk Sprinter port — TODO

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
      TODO extend: nickpass, autoconnect, theme, toggles (original config keys);
      option to store in %NET_DIR%; fall back gracefully if medium is read-only.
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
