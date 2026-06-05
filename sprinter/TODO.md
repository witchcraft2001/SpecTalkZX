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

- [ ] Stage 5b: multi-window/channel model (up to 10), window switching, activity
      indicators, mention highlight.
- [ ] Stage 6: timestamps, nick coloring, notifications, friends, ignore, NickServ
      auto-id, away, help/about/config/status screens.
- [ ] net layer: detect ESP errors (CLOSED/ERROR) in net_connect instead of fixed
      delays; surface connect failures; reconnect.
- [ ] Hardware scroll via BIOS #8A (O(1)) to replace the RAM-ring chat redraw.
- [ ] When code+data outgrow WIN1 (16 KB), allocate WIN2 via Dss.GetMem+SetWin2
      and move buffers there.
- [ ] Stage 7: NE2000 backend behind net.h; packaging + docs.
