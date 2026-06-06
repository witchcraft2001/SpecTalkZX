# SprinTalk 0.1

An IRC client for the **Sprinter** computer running **DSS**, in native 80x32
text mode. It connects to IRC networks (e.g. Libera.Chat) over the network link
brought up by the SprinterWiFi kit, and supports multiple channels and private
chats, scrollback history, Cyrillic (CP866) with on-the-fly UTF-8 recoding, and
NickServ identification.

- **Author:** Dmitry Mikhalchenkov (SprinterTeam)
- **Based on:** the SpecTalk ZX sources (ZX Spectrum IRC client), ported and
  largely rewritten for the Sprinter / DSS platform.
- **Version:** 0.1

## Network backend (ESP / RTL)

SprinTalk is built for a specific network card; the build tag tells you which:

- **ESP** — SprinterWiFi / ESP card over the serial (UART) port. *This is the
  current release.* The title bar shows `SprinTalk 0.1 ESP`.
- **RTL** — RTL8019 (NE2000) ISA Ethernet card. *Planned* — a separate `RTL`
  build will use the same interface and commands.

## What you need

- A Sprinter with DSS and the SprinterWiFi (ESP) card set up.
- `SPTALK.EXE` from the delivery package, copied onto your Sprinter (disk, CF/SD
  card, hard drive — wherever you run programs from). `SPTALK.TXT` (this file) and
  `SPTHOWTO.TXT` (the how-to) are documentation.
- Network already brought up by **NETUP** (see below). SprinTalk reads the
  connection settings published by NETUP, so it needs **no config file of its own**.

## Quick start

1. **Bring the network up first.** Run `NETUP` (from the SprinterWiFi kit) once.
   It joins Wi-Fi and publishes the connection settings (including the serial
   speed) to the system. SprinTalk reads those — if the network is not up it
   prints *"Run NETUP first"* and exits.
2. **Run `SPTALK`.** You start in the **server** window.
3. **Connect:** type `/server <host>` — for example `/server irc.libera.chat`,
   but any IRC server works — and press Enter. Wait for the welcome message.
4. **Join a channel:** `/join #sprinter` (opens a new window).
5. **Chat:** type a line and press Enter. Switch windows with **Tab**.
6. **Quit:** press **Esc** (confirm with Esc again).

On first run a random nick like `SprXXXX` is generated and saved. Your last
server and nick are remembered in `SPTALK.CFG` for next time.

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
/quit                   disconnect
/help                   list commands
```

Any other `/command` (e.g. `/whois`, `/names`, `/list`, `/topic`) is sent to the
server as-is.

## Keys

```
Tab / Shift+Tab   next / previous window
PgUp / PgDn       scroll history up / down
Up / Down         recall previous input lines
Left/Right/Home/End   edit the input line
Esc               quit (press twice)
```

See **SPTHOWTO.TXT** for a fuller walkthrough (channels, private messages,
hiding your IP with a NickServ cloak, encoding, troubleshooting).

## Also in the package

- `SPTHOWTO.TXT` — the full how-to guide.

---
SprinTalk is free software, provided as-is. Based on SpecTalk ZX.
