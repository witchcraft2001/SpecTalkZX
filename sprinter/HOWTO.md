# SprinTalk 0.1 — How-to

A practical guide to using SprinTalk, the Sprinter/DSS IRC client. For the short
version and the command list, see README (SPTALK.TXT).

## 1. Before you start: bring up the network

SprinTalk does not dial Wi-Fi itself. The SprinterWiFi kit's **NETUP** utility
does that once: it joins your access point and publishes the live settings
(IP, gateway, DNS, and the **serial speed**) to the system environment.

Run `NETUP` first. Then run `SPTALK`. If the network is not up, SprinTalk shows
"Wi-Fi is not up — run NETUP first" and exits. The serial speed is taken from
NETUP automatically, so whatever baud you configured there is what SprinTalk uses
(115200, 57600, 230400, …) — you do not set it twice.

## 2. The screen

```
SprinTalk 0.1 ESP  ::  #channel                         23:21:05   <- title + clock
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
- **Switch windows:** **Tab** (next) and **Shift+Tab** (previous).
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

- **"Run NETUP first" on startup** — the network is not up; run `NETUP` and try
  again.
- **Won't connect / no welcome** — check that `NETUP` really joined Wi-Fi, and
  that the server name is correct.
- **"Connection lost" / "Connection timed out"** — the link dropped (the server
  closed it, or there was no data for several minutes and the keepalive got no
  reply). SprinTalk PINGs the server when idle and gives up if the link is dead;
  reconnect with `/server …`.
- **"ESP NOT RESPONDING"** in the status bar — a send could not go out (the ESP
  is wedged or the serial link is stuck). Check NETUP / the card; it clears once
  data flows again.
- **Garbled Russian** — toggle `/encoding`.
- **Settings** — server, port, nick and NickServ password are stored in
  `SPTALK.CFG` in the current directory; delete it to reset.

---
SprinTalk 0.1 — Dmitry Mikhalchenkov (SprinterTeam). Based on SpecTalk ZX.
