# SprinTalk (Sprinter DSS port) — TODO

Status: Stages 1–6 done. Core IRC + UI ported and, in several areas, beyond the
original (paged history, UTF-8 recoding, multi-server, keepalive, completion).
Next milestone: Stage 7 (NE2000 backend + packaging).

## Open

### Stage 7 — NE2000 (RTL8019A) backend + packaging
- [ ] Second network card behind `net.h` (no IRC/UI changes): software stack
      ARP → IPv4 → single-socket TCP (port the RTL8019A sjasm stack's logic to C,
      or wrap it). Reference: `…/sprinter-rtl8019a/src/lib/{rtl8019,arp_lib,tcp_lib}.asm`,
      `…/sprinter-rtl8019a/sprinter_rtl8019_soft.md`.
- [ ] `NET_BACKEND=esp|ne2000` build variant; RTL reads IP/GW/MASK/DNS from env.
- [ ] Ship the RTL build (`make distrib DIST_TAG=rtl` → `sptalk-0.1-rtl.zip`),
      title shows `SprinTalk 0.1 RTL`.

### Needs hardware verification (code done)
- [ ] Ctrl+Tab / Shift+Tab nav, Alt+1..0 channel select, Tab completion.
- [ ] Nick completion from the paged roster (e.g. `/msg Ha`+Tab → Hard in a busy
      channel). `/roster` dumps it.
- [ ] ESP handover: after exit, `wget`/`ftp` work (esp_restore sets CIPMODE=0).
      If it still errors, check persisted baud (UART_DEF) / the kit's hw ESP_RESET.

### Optional feature ports (from the original)
- [ ] `/register <password> <email>` (+ maybe `/verify`): wrapper for the one-time
      NickServ registration, so it isn't typed through `/msg`.
- [ ] About / Config / What's-New as resident text screens (only `/help` exists).
- [ ] `/clear` — clear the current window.
- [ ] Friends list + "online" notifications (the only Stage-6 parity item skipped).
- [ ] Formatted `/whois` / `/names` (currently forwarded raw; numerics shown as-is).

### Enhancements (beyond the original)
- [ ] SASL login — identify during the handshake so a cloak applies before any
      JOIN (host hidden from the very first packet; today we identify post-connect).
- [~] Nick roster is global (one DSS page, capacity 128), fed by PRIVMSG/JOIN/353.
      Possible upgrade: per-channel rosters with full JOIN/PART/QUIT/KICK/NICK
      upkeep, if we want channel-scoped completion of silent users.
- [ ] Multi-server: a small picker UI instead of Up-recall; per-server autojoin.

### Memory watch
- [ ] Code headroom ~2 KB to the WIN1+WIN2 ceiling (0xA800); data+stack nearly
      fill WIN2 (0xA800..0xBFFF). Before large additions, trim code or move more
      data into DSS pages (as the roster/history already do). Beyond 32 KB would
      need code banking (--codeseg) or overlays.

## Done (recent)
- [x] Flat WIN1+WIN2 code layout (crt0_flat, image spans 2 pages, build guard
      `tools/check_layout.py`). Lifted the old WIN1-only ~15.5 KB code cap.
- [x] Multi-window model (server + up to 10), Tab/Ctrl+Tab/Shift+Tab/Alt-digit nav,
      activity (`*`) and mention (`!`) flags, 1-based status numbering.
- [x] Per-window paged scrollback in DSS pages (PgUp/PgDn), BIOS #8A chat scroll.
- [x] Timestamps, nick colouring (hashed DSS attrs), ignore, away, NickServ
      (`/pass` + auto-identify, `/id`), `/me`, `/msg`, `/query`, `/close`.
- [x] UTF-8 ↔ CP866 recoding (`/encoding`); strip mIRC formatting control codes.
- [x] Network state from NETUP env vars (NET/NET_BAUD/…); refuse if NET≠WIFI.
- [x] Keepalive PING + dead-link timeout; ESP "CLOSED" disconnect detection;
      TX-stall detection with an "ESP NOT RESPONDING" status warning.
- [x] Settings in SPTALK.CFG: nick, NickServ pass, recent servers (SRV1..) and
      channels (CHAN1..), seeded into the input recall on startup.
- [x] Tab autocompletion (commands + nicks) with common-prefix + cycle.
- [x] Rebrand to SprinTalk 0.1, ESP/RTL backend tag, SPTALK.EXE, README/HOWTO
      → plain-text docs, `make distrib` zip.

## Not doing (intentionally — ZX cosmetic / low value on Sprinter)
Themes, sound (beep/click), copy/paste, in-buffer search, traffic meter, timezone
command, nick-colour config, the SD-loaded overlay engine (screens are resident).
