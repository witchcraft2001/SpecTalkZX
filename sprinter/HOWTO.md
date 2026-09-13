# SprinTalk 0.2.1 — How-to

A practical guide to using SprinTalk, the Sprinter/DSS IRC client. For the short
version and the command list, see README (SPTALK.TXT).

## 1. Before you start: bring up the network

SprinTalk does not configure the network itself — it uses the link DSS already
has up. Each card has its own bring-up tool, which configures it and publishes
the live settings (IP, gateway, DNS, and for Wi-Fi the **serial speed**) to the
DSS environment, including `NET=<tag>`:

```
Card                          Bring-up tool         NET    DLL
----------------------------  --------------------  -----  ------------
Wi-Fi / ESP (SprinterWiFi)    NETUP                 WIFI   UNETESP.DLL
RTL8019A ISA Ethernet         NETCFG -i, then IFUP  RTL    UNETRTL.DLL
3Com 3C509B ISA Ethernet      NETCFG -i, then IFUP  509B   UNET509B.DLL
```

Run that tool first, then run `SPTALK`. You never set `NET` by hand — the
bring-up tool owns it.

SprinTalk picks the link up by itself: it reads `NET`, loads the matching
`UNETxxxx.DLL` **from its own directory** (the one `SPTALK.EXE` lives in), and
logs both in the server window:

```
UNET: NET=509B -> UNET509B.DLL
Network ready via UNET509B.DLL (UNET509B v0.3.1)
```

So keep the `UNET*.DLL` files next to `SPTALK.EXE`. If the load fails, SprinTalk
prints the reason and keeps running with networking disabled — the UI still
works, see section 14.

On Wi-Fi the serial speed comes from what `NETUP` published, so whatever baud
you configured there is what SprinTalk uses (115200, 57600, 230400, …) — you
do not set it twice. The ISA cards have no baud rate at all.

## 2. The screen

```
SprinTalk 0.2.1 UNET ::  #channel                         23:21:05   <- title + clock
....................................................................
   chat area (history of the current window)
....................................................................
nick:YourNick  [0 >1* 2! ]                                          <- status bar
notifications / hints                                               <- yellow line
> your input line______________________________________________    <- type here
```

Status bar window list: each number is an open window; `>` marks the current one,
`*` means unread activity, `!` means you were mentioned. Window `0` is always the
**server** window (system messages, notices, command replies).

## 3. Connecting

In the server window, type:

```
/server irc.libera.chat
```

(Optionally add a port: `/server irc.libera.chat 6667`.) Wait for the welcome /
MOTD text. Your nick and the last few servers you used are saved to `SPTALK.CFG`;
next time press **Up** to cycle through the recent `/server …` lines and Enter to
connect.

To change your nick: `/nick NewName`.

## 4. Channels and windows

- **Join:** `/join #sprinter` — opens a new window and switches to it.
- **Switch windows:** **Ctrl+Tab** (next) and **Shift+Tab** (previous).
- **Jump to a channel:** **Alt+1..Alt+9**, and **Alt+0** for the 10th. Channels are
  numbered from 1 in the status bar (the server window shows as `S`).
- **Tab** completes: after `/` it completes a **command**; otherwise it completes
  a **nick** of someone you've recently seen (so you can quickly start a `/msg`
  or address someone). If several match, the first Tab fills the common prefix
  and lists the candidates on the notice line; press Tab again to cycle through
  them.
- **Leave:** `/part` leaves the channel in the current window.
- **Close:** `/close` closes the current window — `/part`s it first if it is a
  channel, or just closes a private window. (The server window can't be closed.)
- **Talk:** type a message and press Enter. Your own messages are echoed as
  `<YourNick> text`. Other people appear as `<Their Nick> text`, each nick in its
  own colour (the same person always gets the same colour).

You can have several channels and private chats open at once; only the current
window is shown, the others raise the `*`/`!` flags in the status bar.

## 5. Private messages

Two ways:

- **`/query <nick>`** — opens an empty private window for that person. Switch to
  it and just type to talk to them.
- **`/msg <nick> <text>`** — sends one private line immediately; a private window
  for that nick is opened automatically and your message echoed there. If you are
  in another window you also get a `-> nick` confirmation on the notice line.

When someone messages **you**, a private window with their nick opens by itself.

## 6. Actions: /me

`/me <action>` sends a "third person" action. If your nick is Dimko,

```
/me waves hello
```

everyone (and you) sees `* Dimko waves hello`. Use it for emotes.

## 7. NickServ and hiding your IP

On most networks your account is managed by a service called **NickServ**, and
to use a registered nick you must identify with a password.

- **Save your password:** `/pass yourpassword` — stored in `SPTALK.CFG` and used
  to **auto-identify**: when NickServ asks you to identify after connecting,
  SprinTalk answers automatically. (`/pass` alone shows whether one is set;
  `/pass clear` removes it.)
- **Identify now:** `/id` (uses the saved password) or `/id yourpassword`.

**Hiding your IP from other users.** When you join a channel, the IRC *server*
shows your host/IP to everyone — that is the protocol, not SprinTalk leaking it
(SprinTalk itself never displays other people's hosts). To hide it you need a
**cloak** from the network:

1. Register your nick with NickServ (one time). On Libera:
   `/msg NickServ REGISTER <password> <your-email>`, then confirm with the code
   they e-mail you: `/msg NickServ VERIFY REGISTER <nick> <code>`.
2. In SprinTalk, `/pass <password>` so you identify on every connect.
3. Request a cloak from the network (on Libera, ask in `#libera`). Once granted,
   your host shows as `user/<account>` instead of your provider address.

Note: SprinTalk identifies just after connecting, so let auto-identify happen
before you `/join` — the cloak applies once you are identified.

> Security: the password is stored in plain text in the `SPTALK.CFG` file.

## 8. Cyrillic and encoding

The Sprinter text screen uses **CP866**. Modern IRC is **UTF-8**. SprinTalk
recodes both ways automatically so Russian text is readable on public channels:
incoming UTF-8 → CP866 for display, your CP866 input → UTF-8 when sending.

Toggle it with **`/encoding`** (or `/enc`). Turn it off if you are on a channel
that already uses CP866/raw bytes.

## 9. Timestamps, ignore, away

- **`/timestamp`** (`/ts`) — toggle the `[HH:MM]` prefix on messages.
- **`/ignore <nick>`** — hide messages from a nick; `/ignore` alone lists the
  ignored nicks; `/ignore <nick>` again removes them.
- **`/away [message]`** — mark yourself away (no message = back).

## 10. Reading history

Each window keeps its own scrollback. **PgUp** / **PgDn** scroll the current
window up and down; scroll back to the bottom to return to live messages.

## 11. Editing the input line

```
Up / Down       recall previous / next input lines you typed
Left / Right     move the cursor
Home / End       jump to start / end
Backspace        delete the character before the cursor
```

The input line scrolls sideways if it is longer than the screen (a `<` marks
hidden text to the left).

## 12. Other commands

Any `/command` SprinTalk doesn't handle is forwarded to the server unchanged, so
the usual ones work: `/whois <nick>`, `/names`, `/list`, `/topic`, `/mode`, …
Their replies appear in the server window. `/raw <text>` sends a raw IRC line.

## 13. Quitting

Press **Esc**, then **Esc** again to confirm (any other key cancels). SprinTalk
sends `QUIT`, closes the link cleanly, and returns to DSS.

## 14. Troubleshooting

- **"Run NETUP first" on startup** — Wi-Fi is not up; run `NETUP` and try
  again.
- **"UI works, networking unavailable"** — the driver
  could not be loaded, or the link is down. SprinTalk does not exit; the line
  just above it says why:
  - *"env NET is empty - run the card's setup tool first"* — you did not run the
    bring-up tool (section 1).
  - *"DLL not found next to SPTALK.EXE (or unreadable)"* — copy the `UNET*.DLL`
    files into the same directory as `SPTALK.EXE`. The `tried:` line printed
    below it is the exact path that was opened.
  - *"DLL's own name does not match env NET"* — the DLL on disk is not the one
    `NET` asks for; copy a fresh, matching set of `UNET*.DLL` files over.
  - *"NETINIT failed - card present but not ready"*, or
    *"DLL reports the link is down"* — the card did not come up. Re-run the
    bring-up tool and check what it says.
- **Won't connect / no welcome** — check that the network really came up (the
  card kit's own ping/test tool), and that the server name is correct.
- **"Connection lost" / "Connection timed out"** — the link dropped (the server
  closed it, or there was no data for several minutes and the keepalive got no
  reply). SprinTalk PINGs the server when idle and gives up if the link is dead;
  reconnect with `/server …`.
- `*** LINK NOT RESPONDING ***` in the status bar — a send could not go out
  (the card is wedged, or the serial link to the ESP is stuck). Check the card /
  re-run the bring-up tool; it clears once data flows again.
- **Garbled Russian** — toggle `/encoding`.
- **Settings** — server, port, nick and NickServ password are stored in
  `SPTALK.CFG` in the current directory; delete it to reset.

---
Based on SpecTalk ZX.

Dmitry Mikhalchenkov (SprinterTeam).
FidoNet: 2:5030/1997.10