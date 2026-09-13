```
  ▄▄▄▄▄                ▄▄▄▄▄▄▄     ▄▄        
 ██▀▀▀▀█▄             █▀▀██▀▀▀▀     ██       
 ▀██▄  ▄▀       ▀▀ ▄     ██         ██ ▄▄    
   ▀██▄▄  ████▄ ██ ████▄ ██   ▄▀▀█▄ ██ ██ ▄█▀
 ▄   ▀██▄ ██ ██ ██ ██ ██ ██   ▄█▀██ ██ ████  
 ▀██████▀▄████▀▄██▄██ ▀█ ▀██▄▄▀█▄██▄██▄██ ▀█▄
          ██                                 
          ▀                                  
```
# SprinTalk 0.2.1

An IRC client for the **Sprinter** computer running **DSS**, in native 80x32
text mode. It connects to IRC networks (e.g. Libera.Chat) over whatever network
card DSS already has up — Wi-Fi (SprinterWiFi/ESP), RTL8019A or 3Com 3C509B ISA
Ethernet — and supports multiple channels and private chats, scrollback history,
Cyrillic (CP866) with on-the-fly UTF-8 recoding, and NickServ identification.

- **Author:** Dmitry Mikhalchenkov, SprinterTeam. 2:5030/1997.10
- **Based on:** the SpecTalk ZX sources (ZX Spectrum IRC client), ported and
  largely rewritten for the Sprinter / DSS platform.
- **Version:** 0.2.1

## Two builds, one program

SprinTalk is packaged twice. The tag after the version in the title bar says
which one you started.

- **`sptalk-<ver>-unet.zip` — the general one.** Talks to the card through a
  `UNETxxxx.DLL` driver, so a single `SPTALK.EXE` covers all three cards: at
  startup it reads the `NET` environment variable and loads the matching DLL
  from its own directory. Keep the three `UNET*.DLL` files beside `SPTALK.EXE`.
- **`sptalk-<ver>-esp.zip`** — the self-contained Wi-Fi-only build with the ESP
  UART driver compiled in. No DLLs, no `NET` lookup.

## What you need

- A Sprinter with DSS and one of the supported cards, already brought up by that
  card's own tool:

```
Card                          Bring-up tool         NET    DLL (UNET build)
----------------------------  --------------------  -----  ----------------
Wi-Fi / ESP (SprinterWiFi)    NETUP                 WIFI   UNETESP.DLL
RTL8019A ISA Ethernet         NETCFG -i, then IFUP  RTL    UNETRTL.DLL
3Com 3C509B ISA Ethernet      NETCFG -i, then IFUP  509B   UNET509B.DLL
```

  The bring-up tool ships with the card's own kit and publishes `NET` plus the
  live IP/gateway/DNS settings to the DSS environment. Never set `NET` by hand.
- The package contents copied onto your Sprinter (disk, CF/SD card, hard drive —
  wherever you run programs from): `SPTALK.EXE`, the `UNET*.DLL` files if you
  took the UNET package, and the docs `SPTALK.TXT` (this file) and
  `HOWTO.TXT` / `HOWTO_RU.TXT`.
- Nothing else: SprinTalk reads the settings the bring-up tool published, so it
  needs **no network config file of its own**.

## Quick start

1. **Bring the network up first**, with your card's tool from the table above —
   `NETUP` for Wi-Fi, `NETCFG -i` then `IFUP` for the ISA cards. SprinTalk reads
   what it published. If nothing is up, the UNET build reports
   *"env NET is empty - run the card's setup tool first"* and runs with
   networking disabled; the ESP build prints *"Run NETUP first"* and exits.
2. **Run `SPTALK`.** You start in the **server** window. The UNET build logs the
   card and driver it picked there, e.g. `UNET: NET=509B -> UNET509B.DLL`.
3. **Connect:** type `/server <host>` — for example `/server irc.libera.chat`,
   but any IRC server works — and press Enter. Wait for the welcome message.
4. **Join a channel:** `/join #sprinter` (opens a new window).
5. **Chat:** type a line and press Enter. Switch windows with **Tab**.
6. **Quit:** press **Esc** (confirm with Esc again).

On first run a random nick like `SprXXXX` is generated and saved. Your nick and
the last few servers you connected to are remembered in `SPTALK.CFG`; press **Up**
to cycle through the recent `/server …` lines.

## Common commands

```
/server <host> [port]   connect (default port 6667)
/nick <name>            change your nick
/join #channel          join a channel (new window)
/query <nick>           open a private chat window
/msg <target> <text>    send a private message
/me <action>            send an action  (* you wave)
/pass <password>        save NickServ password (auto-identify on connect)
/id [password]          identify with NickServ now
/encoding               toggle UTF-8 <-> CP866 recoding
/timestamp              toggle [HH:MM] timestamps
/part                   leave the current channel
/close                  close the current window (channel or private)
/quit                   disconnect
/help                   list commands
```

Any other `/command` (e.g. `/whois`, `/names`, `/list`, `/topic`) is sent to the
server as-is.

## Keys

```
Tab               autocomplete a command (after /) or a recent nick
Ctrl+Tab / Shift+Tab   next / previous window
Alt+1..9, Alt+0   jump to channel 1..9 / 10
PgUp / PgDn       scroll history up / down
Up / Down         recall previous input lines
Left/Right/Home/End   edit the input line
Esc               quit (press twice)
```

See **HOWTO.TXT** or **HOWTO_RU.TXT** for a fuller walkthrough (channels,
private messages, hiding your IP with a NickServ cloak, encoding,
troubleshooting).

## Also in the package

- `HOWTO.TXT` — the full how-to guide.
- `HOWTO_RU.TXT` — the same guide in Russian (CP866).
- `UNETESP.DLL`, `UNETRTL.DLL`, `UNET509B.DLL` (UNET package only) — the network
  backends. Keep them in the same directory as `SPTALK.EXE`; SprinTalk loads one
  of them at startup and ignores the rest.

---
SprinTalk is free software, provided as-is. Based on SpecTalk ZX.
