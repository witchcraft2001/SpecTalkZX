/*
 * SpecTalk ZX — Sprinter DSS port
 * Stage 5b: multi-window IRC client.
 *
 * Thin shell: input editor + command parsing here; protocol/windows/rendering
 * live in irc.c, transport in net_esp.c. Commands: /server /nick /join /part
 * /quit /raw /help /win. Plain text -> message current window. Tab switches
 * windows. Double-ESC quits.
 *
 * Copyright (C) 2026 M. Ignacio Monge Garcia — GPLv2 (see ../../LICENSE)
 */

#include "version.h"
#include "term.h"
#include "net.h"
#include "irc.h"
#include "cfg.h"

#define K_ESC   0x1B
#define K_ENTER 0x0D
#define K_BS    0x08
#define K_TAB   0x09
#define SC_TAB   0x0F
#define SC_LEFT  0x54
#define SC_RIGHT 0x56
#define SC_UP    0x58
#define SC_DOWN  0x52
#define SC_HOME  0x57
#define SC_END   0x51
#define SC_PGUP  0x59
#define SC_PGDN  0x53

#define IN_MAX    400   /* IRC line limit is 512B incl CRLF; ~400 text is the safe cap */
#define HIST_N    8

/* ---- small string helpers ------------------------------------------ */
static u8 starts(const char *s, const char *p) { while (*p) { if (*s != *p) return 0; s++; p++; } return 1; }
static void s_cpy(char *d, const char *s, u8 max) { u8 i = 0; while (s[i] && i < (u8)(max - 1)) { d[i] = s[i]; i++; } d[i] = 0; }

static settings_t S;     /* persisted last server/port/nick */

/* ---- editor state -------------------------------------------------- */
static char inbuf[IN_MAX];
static u16  inlen, incur;
static u8   rxb[256];

/* Recall history lives in its own 16K DSS page — full-length entries, never
 * truncated. Slot i holds one entry (up to IN_MAX bytes); HIST_N slots ring. */
#define EH_SLOT IN_MAX
static u8   eh_page;            /* DSS block id (0xFF = none) */
static u16  ehl[HIST_N];        /* length of each ring slot's entry */
static u8   ehcount, ehfirst, ehbrowse;

static void redraw(void) { term_input(inbuf, inlen, incur); }

static void ins_char(u8 c) {
    u16 i;
    if (inlen >= IN_MAX - 1) return;
    if (incur > inlen) incur = inlen;
    for (i = inlen; i > incur; i--) inbuf[i] = inbuf[i - 1];
    inbuf[incur] = (char)c; inlen++; incur++;
}
static void del_before(void) {
    u16 i;
    if (incur == 0) return;
    for (i = incur - 1; i < inlen - 1; i++) inbuf[i] = inbuf[i + 1];
    inlen--; incur--;
}
static void eh_init(void) {
    eh_page = dss_getmem();              /* one 16K page; 0xFF if it fails */
    ehcount = 0; ehfirst = 0; ehbrowse = 0;
}

/* store `len` bytes from `src` into ring slot `ring` (page mapped to WIN3) */
static void eh_store(u8 ring, const char *src, u16 len) {
    char *dst; u16 i;
    if (eh_page == 0xFF) return;
    if (len > EH_SLOT) len = EH_SLOT;
    dss_setwin(3, eh_page);              /* map page; no DSS calls until after the copy */
    dst = (char *)(0xC000 + (u16)ring * EH_SLOT);
    for (i = 0; i < len; i++) dst[i] = src[i];
    ehl[ring] = len;
}

/* append an entry to the recall ring (full length, no truncation) */
static void eh_push(const char *src, u16 len) {
    u8 ring;
    if (len == 0) return;
    if (ehcount < HIST_N) { ring = (u8)((ehfirst + ehcount) % HIST_N); ehcount++; }
    else { ring = ehfirst; ehfirst = (u8)((ehfirst + 1) % HIST_N); }
    eh_store(ring, src, len);
    ehbrowse = ehcount;
}

static void eh_seed(const char *s) {
    u16 len = 0; while (s[len]) len++;
    eh_push(s, len);
}

/* load logical entry `logical` (0 = oldest) from the page into inbuf */
static void eh_load(u8 logical) {
    u8 ring = (u8)((ehfirst + logical) % HIST_N);
    u16 len, i;
    const char *src;
    if (eh_page == 0xFF) { inlen = 0; incur = 0; return; }
    len = ehl[ring];
    dss_setwin(3, eh_page);
    src = (const char *)(0xC000 + (u16)ring * EH_SLOT);
    for (i = 0; i < len; i++) inbuf[i] = src[i];
    inlen = len; incur = len;
}

static void save_settings(void) {
    s_cpy(S.nick, irc_nick_str(), sizeof(S.nick));
    s_cpy(S.nspass, irc_nspass(), sizeof(S.nspass));
    cfg_save(&S);
}

/* default nick "SprXXXX" with a 16-bit hex from the current date/time, so two
 * machines running the stock client don't collide. Persisted until /nick. */
static void gen_nick(char *o) {
    static const char hx[] = "0123456789ABCDEF";
    dss_time_t t;
    dss_date_t d;
    u16 v;
    dss_gettime(&t);
    dss_getdate(&d);
    v = (u16)((u16)t.hour * 3607u + (u16)t.minute * 61u + t.second
              + (u16)d.day * 131u + (u16)d.month * 17u + d.year);
    o[0] = 'S'; o[1] = 'p'; o[2] = 'r';
    o[3] = hx[(v >> 12) & 0xF]; o[4] = hx[(v >> 8) & 0xF];
    o[5] = hx[(v >> 4) & 0xF];  o[6] = hx[v & 0xF];
    o[7] = 0;
}

/* ---- Tab autocompletion (commands after '/', else nicks) ----------- */
static const char *const CMD_TBL[] = {
    "server", "nick", "join", "query", "msg", "me", "id", "pass", "close",
    "part", "quit", "win", "raw", "ignore", "away", "timestamp", "encoding",
    "help", 0
};
static u8   comp_active;          /* mid completion cycle */
static u16  comp_pos;             /* word start in inbuf */
static u8   comp_cmd;             /* completing a command (vs a nick) */
static u8   comp_idx;             /* last candidate inserted (0xFF = showed prefix) */
static char comp_base[24];        /* the prefix the user originally typed */

static u8 lc1(u8 c) { return (c >= 'A' && c <= 'Z') ? (u8)(c + 32) : c; }
static u8 starts_ci(const char *s, const char *p) {
    while (*p) { if (lc1((u8)*s) != lc1((u8)*p)) return 0; s++; p++; }
    return 1;
}
static u8 cand_count(const char *pfx, u8 cmd) {
    u8 n = 0, i;
    if (cmd) { for (i = 0; CMD_TBL[i]; i++) if (starts_ci(CMD_TBL[i], pfx)) n++; }
    else { u8 t = irc_nick_n(); for (i = 0; i < t; i++) if (starts_ci(irc_nick_at(i), pfx)) n++; }
    return n;
}
/* copy the k-th candidate matching pfx into out[<=24] (irc_nick_at shares one
 * buffer, so candidates must be copied out, not held by pointer) */
static void cand_get(const char *pfx, u8 cmd, u8 k, char *out) {
    u8 i, n = 0;
    out[0] = 0;
    if (cmd) { for (i = 0; CMD_TBL[i]; i++) if (starts_ci(CMD_TBL[i], pfx)) { if (n == k) { s_cpy(out, CMD_TBL[i], 24); return; } n++; } }
    else { u8 t = irc_nick_n(); for (i = 0; i < t; i++) { const char *x = irc_nick_at(i); if (starts_ci(x, pfx)) { if (n == k) { s_cpy(out, x, 24); return; } n++; } } }
}

/* replace inbuf[from..incur) with text(+sep), preserving any tail after incur */
static void put_word(u16 from, const char *text, const char *sep) {
    u16 tlen = 0, slen = 0, add, tail, i;
    while (text[tlen]) tlen++;
    while (sep[slen]) slen++;
    add = (u16)(tlen + slen);
    tail = (u16)(inlen - incur);
    if ((u16)(from + add + tail) > IN_MAX - 1) {
        if ((u16)(from + add) > IN_MAX - 1) return;
        tail = (u16)((IN_MAX - 1) - (from + add));
    }
    if (add > (u16)(incur - from)) { for (i = tail; i-- > 0;) inbuf[from + add + i] = inbuf[incur + i]; }
    else { for (i = 0; i < tail; i++) inbuf[from + add + i] = inbuf[incur + i]; }
    for (i = 0; i < tlen; i++) inbuf[from + i] = text[i];
    for (i = 0; i < slen; i++) inbuf[from + tlen + i] = sep[i];
    inlen = (u16)(from + add + tail);
    incur = (u16)(from + add);
}

static void do_complete(void) {
    u16 from, pstart;
    u8 cmd, n, i, pl;
    char prefix[24], cand[24];
    const char *sep;

    if (comp_active) {                              /* cycle to the next candidate */
        n = cand_count(comp_base, comp_cmd);
        if (n) {
            comp_idx = (u8)((comp_idx + 1) % n);
            cand_get(comp_base, comp_cmd, comp_idx, cand);
            sep = comp_cmd ? " " : (comp_pos == 0 ? ": " : " ");
            put_word(comp_pos, cand, sep);
        }
        return;
    }

    from = incur;
    while (from > 0 && inbuf[from - 1] != ' ') from--;
    pstart = from; cmd = 0;
    if (pstart == 0 && inbuf[0] == '/') { cmd = 1; pstart = 1; }
    pl = 0;
    for (i = 0; (u16)(pstart + i) < incur && pl < (u8)(sizeof(prefix) - 1); i++) prefix[pl++] = inbuf[pstart + i];
    prefix[pl] = 0;
    if (pl == 0) { if (!cmd) term_notif("type a few letters, then Tab"); return; }

    n = cand_count(prefix, cmd);
    if (n == 0) { term_notif("no match"); return; }
    sep = cmd ? " " : (pstart == 0 ? ": " : " ");
    if (n == 1) { cand_get(prefix, cmd, 0, cand); put_word(pstart, cand, sep); return; }

    /* multiple matches: extend to the longest common prefix, then arm cycling */
    {
        char lcp[24], ck[24]; u8 ll;
        cand_get(prefix, cmd, 0, lcp);
        for (ll = 0; lcp[ll]; ll++) ;
        for (i = 1; i < n; i++) {
            u8 j = 0;
            cand_get(prefix, cmd, i, ck);
            while (j < ll && lc1((u8)lcp[j]) == lc1((u8)ck[j])) j++;
            ll = j; lcp[ll] = 0;
        }
        for (i = 0; i < pl; i++) comp_base[i] = prefix[i];
        comp_base[pl] = 0;
        comp_cmd = cmd; comp_pos = pstart; comp_active = 1;
        if (ll > pl) { comp_idx = 0xFF; put_word(pstart, lcp, ""); }
        else { comp_idx = 0; cand_get(prefix, cmd, 0, cand); put_word(pstart, cand, sep); }
        {                                           /* list the candidates on the notif line */
            char m[80]; u8 mp = 0, k;
            for (k = 0; k < n && mp < 76; k++) {
                u8 j = 0;
                cand_get(prefix, cmd, k, ck);
                while (ck[j] && mp < 78) m[mp++] = ck[j++];
                if (mp < 78) m[mp++] = ' ';
            }
            m[mp] = 0; term_notif(m);
        }
    }
}

/* ---- commands ------------------------------------------------------ */
static char *next_arg(char *s) {
    while (*s && *s != ' ') s++;
    while (*s == ' ') *s++ = 0;
    return s;
}

static void do_command(char *line) {
    char *cmd = line + 1;
    char *arg = next_arg(cmd);

    if (starts(cmd, "server")) {
        char *host = arg, *port = next_arg(arg);
        if (!host[0]) { term_notif("usage: /server <host> [port]"); return; }
        if (!port[0]) port = "6667";
        term_notif("connecting...");
        if (irc_connect(host, port) == NET_OK) {
            cfg_add_server(&S, host, port); /* remember it (recent-server list) */
            save_settings();
        } else {
            term_notif("connect failed - check link/server, try again");
        }
    } else if (starts(cmd, "nick")) {
        if (arg[0]) { irc_set_nick(arg); save_settings(); }
    } else if (starts(cmd, "join")) {
        if (arg[0]) { irc_join(arg); cfg_add_chan(&S, arg); save_settings(); }
    } else if (starts(cmd, "query")) {
        if (arg[0]) irc_query(arg); else term_notif("usage: /query <nick>");
    } else if (starts(cmd, "msg")) {
        char *target = arg, *text = next_arg(arg);
        if (!target[0] || !text[0]) { term_notif("usage: /msg <target> <text>"); return; }
        irc_msg(target, text);
    } else if (starts(cmd, "me")) {
        if (arg[0]) irc_me(arg); else term_notif("usage: /me <action>");
    } else if (starts(cmd, "id")) {                 /* identify with NickServ */
        if (arg[0]) { irc_set_nspass(arg); save_settings(); }
        irc_identify(arg);
    } else if (starts(cmd, "pass")) {               /* set/show/clear NickServ password */
        if (!arg[0]) term_notif(irc_nspass()[0] ? "NickServ password: set" : "NickServ password: not set");
        else if (starts(arg, "clear") || starts(arg, "none")) { irc_set_nspass(""); save_settings(); term_notif("password cleared"); }
        else { irc_set_nspass(arg); save_settings(); term_notif("password saved (auto-identify on)"); }
    } else if (starts(cmd, "close")) {
        irc_close();
    } else if (starts(cmd, "part")) {
        irc_part();
    } else if (starts(cmd, "quit")) {
        irc_quit(); term_notif("disconnected");
    } else if (starts(cmd, "win")) {
        if (arg[0] >= '0' && arg[0] <= '9') irc_select_chan((u8)(arg[0] - '0'));  /* /win N */
        else irc_next_window();
    } else if (starts(cmd, "roster")) {
        irc_list_nicks();
    } else if (starts(cmd, "raw")) {
        irc_raw(arg);
    } else if (starts(cmd, "ignore")) {
        irc_ignore(arg);
    } else if (starts(cmd, "away")) {
        irc_away(arg);
    } else if (starts(cmd, "timestamp") || starts(cmd, "ts")) {
        irc_toggle_ts();
    } else if (starts(cmd, "encoding") || starts(cmd, "enc")) {
        irc_toggle_encoding();
    } else if (starts(cmd, "help")) {
        irc_local("Commands:");
        irc_local("  /server <host> [port]  - connect (default 6667)");
        irc_local("  /nick <name>           - change nick");
        irc_local("  /join #channel         - join (opens a window)");
        irc_local("  /query <nick>          - open a private window");
        irc_local("  /msg <target> <text>   - send a private message");
        irc_local("  /me <action>           - send an action (* you ...)");
        irc_local("  /pass <password>       - save NickServ pass (auto-identify)");
        irc_local("  /id [password]         - identify with NickServ now");
        irc_local("  /part                  - leave current channel");
        irc_local("  /close                 - close current window (channel or query)");
        irc_local("  /quit                  - disconnect");
        irc_local("  /raw <text>            - send a raw IRC line");
        irc_local("  /ignore <nick>         - toggle ignoring a nick");
        irc_local("  /away [message]        - set/clear away");
        irc_local("  /timestamp (/ts)       - toggle message timestamps");
        irc_local("  /encoding (/enc)       - toggle UTF-8 <-> CP866");
        irc_local("  /whois /list /names... - any other /cmd is sent to the server");
        irc_local("  text                   - message current window");
        irc_local("  Tab=complete (cmd/nick), Ctrl+Tab/Shift+Tab=window, Alt+1..0=channel");
        irc_local("  arrows/Home/End edit, Up/Down input history, PgUp/PgDn scroll");
    } else {
        irc_send_cmd(cmd, arg);   /* forward unknown /cmd to the server (/whois, /list, ...) */
    }
}

static void on_enter(void) {
    inbuf[inlen] = 0;
    if (inlen == 0) return;
    eh_push(inbuf, inlen);
    if (inbuf[0] == '/') do_command(inbuf);
    else irc_say(inbuf);
    inlen = 0; incur = 0;
}

/* ---- main ---------------------------------------------------------- */
void main(void) {
    dss_key_t key, consume;
    u16 i, n;
    u8 quit_pending = 0;
    u8 was_conn = 0;

    term_init();
    term_notif("Starting up...");                         /* immediate feedback while loading */
    cfg_load(&S);
    if (!S.nick[0]) { gen_nick(S.nick); cfg_save(&S); }   /* unique default nick, persisted */
    irc_init(S.nick);
    irc_set_nspass(S.nspass);                             /* enable NickServ auto-identify */

    irc_local(APP_TITLE " by " APP_AUTHOR);               /* startup banner: author + build */
    irc_local("build " APP_BUILD);

    inlen = 0; incur = 0;
    eh_init();
    for (i = 0; i < IN_MAX; i++) inbuf[i] = 0;

    term_notif("Initializing network (ESP), please wait...");
    {
        i8 nr = net_init();
        if (nr == NET_NO_LINK) {            /* NETUP not run -> message and exit */
            dss_clrscr();
            dss_gotoxy(1, 1);
            dss_puts(APP_TITLE "\r\n\r\n");
            dss_puts("Wi-Fi is not up (env NET != WIFI).\r\n");
            dss_puts("Run NETUP first to bring the network up,\r\n");
            dss_puts("then start " APP_NAME " again.\r\n\r\n");
            dss_puts("Press any key to exit.\r\n");
            dss_waitkey();
            dss_exit(1);
        }
        if (nr == NET_NO_HW) {
            irc_local("No SprinterWiFi (ESP) UART detected.");
            irc_local("UI works, networking unavailable.");
        } else {
        char m[48], *o = m;
        const char *p = "ESP ready. UART baud=";
        const char *b = net_cfg_baud();
        u8 d = net_cfg_div();
        while (*p) *o++ = *p++;
        if (*b) { while (*b) *o++ = *b++; } else { *o++ = '('; *o++ = 'd'; *o++ = 'e'; *o++ = 'f'; *o++ = ')'; }
        *o++ = ' '; *o++ = 'd'; *o++ = 'i'; *o++ = 'v'; *o++ = '=';
        *o++ = (char)('0' + (d / 100) % 10); *o++ = (char)('0' + (d / 10) % 10); *o++ = (char)('0' + d % 10);
        *o++ = ' '; *o++ = 's'; *o++ = 'l'; *o++ = 'o'; *o++ = 't'; *o++ = '=';
        *o++ = (char)('0' + (net_cfg_slot() & 1));
        *o = 0;
        irc_local(m);
        irc_local("/help for commands.");
        }
    }

    /* Seed the input recall: channels first, then servers, so the most recent
     * /server is what Up offers first (you connect before you join). */
    if (S.loaded) {
        u8 k = S.nchan;
        while (k--) {
            char seed[40], *o = seed;
            const char *p = "/join ";
            while (*p) *o++ = *p++;
            p = S.chan[k]; while (*p) *o++ = *p++;
            *o = 0;
            eh_seed(seed);
        }
    }
    if (S.loaded && S.nsrv) {                /* offer recent servers via the recall */
        u8 si = S.nsrv;
        while (si--) {                       /* push oldest..newest so newest recalls first */
            char seed[72], *o = seed;
            const char *p = "/server ";
            while (*p) *o++ = *p++;
            p = S.srv[si]; while (*p) *o++ = *p++;
            *o = 0;
            eh_seed(seed);
        }
        irc_local("Recent servers/channels: press Up to recall, ENTER to run.");
    } else {
        irc_local("Try:  /server irc.libera.chat   then  /join #test");
    }
    term_notif("/help | Tab=complete | Ctrl/Shift+Tab=window | Alt+1..0=chan | ESC=exit");
    redraw();

    for (;;) {
        /* RX flow control (mirrors the SprinterWiFi ftp/wget driver): raise RTS,
         * drain the ESP's burst, then drop RTS so the ESP holds its TX for the
         * whole slow render below. Without this the render outran the FIFO and
         * lost messages, desyncing the IRC state (broken /join). See uart.c. */
        net_rx_resume();
        n = net_poll(rxb, sizeof(rxb));   /* drain UART first, before slower work */
        net_rx_pause();
        if (n) irc_feed(rxb, n);

        if (net_overrun()) {              /* RX overran anyway -> bytes lost, state may be off */
            net_clear_overrun();
            term_notif("WARNING: link overrun, some messages may be lost");
        }

        if (was_conn && !irc_connected()) {   /* link dropped (ESP reported CLOSED) */
            irc_on_disconnect();
            term_notif("Connection lost (server closed the link)");
        }
        was_conn = irc_connected();
        irc_keepalive();                      /* PING when idle; drop a dead link after a timeout */

        if (net_stalled()) {                  /* a send couldn't drain: ESP wedged */
            irc_net_warn(1);
            term_notif("WARNING: ESP not responding. Check the link / NETUP.");
            net_clear_stall();                /* re-arm so recovery can clear it */
        } else if (n) {
            irc_net_warn(0);                  /* RX flowing again -> link healthy */
        }

        term_clock();

        if (dss_testkey(&key)) {
            u8 is_tab, plain_tab;
            dss_scankey(&consume);
            /* With Ctrl/Alt held the keyboard reports ascii=0 and sets bit 0x80 in
             * scan, so match Tab on the masked scan code too (fixes Ctrl+Tab). */
            is_tab = (key.ascii == K_TAB) || ((key.scan & 0x7F) == SC_TAB);
            plain_tab = is_tab && !(key.modifiers & (DSS_KEYMOD_LSHIFT | DSS_KEYMOD_RSHIFT |
                        DSS_KEYMOD_CTRL | DSS_KEYMOD_LCTRL | DSS_KEYMOD_RCTRL));
            if (!plain_tab) comp_active = 0;        /* any other key ends a completion cycle */
            if (quit_pending) {
                quit_pending = 0;
                if (key.ascii == K_ESC) break;
                term_notif("quit cancelled");
                continue;
            }
            if (key.ascii == K_ESC) { quit_pending = 1; term_notif("Press ESC again to quit, any other key to cancel"); }
            else if (is_tab) {                       /* Tab/Shift+Tab/Ctrl+Tab */
                if (key.modifiers & (DSS_KEYMOD_LSHIFT | DSS_KEYMOD_RSHIFT)) { irc_prev_window(); redraw(); }
                else if (key.modifiers & (DSS_KEYMOD_CTRL | DSS_KEYMOD_LCTRL | DSS_KEYMOD_RCTRL)) { irc_next_window(); redraw(); }
                else if (inlen == 0) { irc_next_window(); redraw(); }   /* empty line: Tab cycles windows */
                else { do_complete(); redraw(); }                       /* otherwise: complete */
            }
            else if (key.ascii == K_ENTER) { on_enter(); redraw(); }
            else if (key.ascii == K_BS) { del_before(); redraw(); }
            else if (key.scan == SC_LEFT)  { if (incur > 0) { incur--; redraw(); } }
            else if (key.scan == SC_RIGHT) { if (incur < inlen) { incur++; redraw(); } }
            else if (key.scan == SC_HOME)  { incur = 0; redraw(); }
            else if (key.scan == SC_END)   { incur = inlen; redraw(); }
            else if (key.scan == SC_PGUP)  { irc_scroll_up(); redraw(); }
            else if (key.scan == SC_PGDN)  { irc_scroll_down(); redraw(); }
            else if (key.scan == SC_UP)    { if (ehbrowse > 0) { ehbrowse--; eh_load(ehbrowse); redraw(); } }
            else if (key.scan == SC_DOWN)  { if (ehbrowse < ehcount) { ehbrowse++; if (ehbrowse == ehcount) { inlen = 0; incur = 0; } else eh_load(ehbrowse); redraw(); } }
            else if ((key.modifiers & (DSS_KEYMOD_ALT | DSS_KEYMOD_LALT | DSS_KEYMOD_RALT))
                     && (key.scan & 0x7F) >= 0x02 && (key.scan & 0x7F) <= 0x0B) {
                /* Alt+digit: ascii is 0, so use the scan code. PC set: 1..9,0 = 0x02..0x0B. */
                u8 s7 = (u8)(key.scan & 0x7F);
                irc_select_chan((u8)(s7 == 0x0B ? 0 : s7 - 1));   /* 1..9 -> win 1..9, 0 -> win 10 */
                redraw();
            }
            else if ((key.ascii >= 32 && key.ascii < 127) || key.ascii >= 0x80) { ins_char(key.ascii); redraw(); }
        }
    }

    term_notif("Closing link, restoring ESP...");
    if (irc_connected()) irc_quit();   /* QUIT + net_close (restores ESP) */
    else net_close();                  /* link already down: still restore CIPMODE=0 */
    dss_clrscr();
    dss_gotoxy(1, 1);
    dss_puts(APP_NAME " - bye.\r\n");
    dss_exit(0);
}
