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

#include "term.h"
#include "net.h"
#include "irc.h"
#include "cfg.h"

#define K_ESC   0x1B
#define K_ENTER 0x0D
#define K_BS    0x08
#define K_TAB   0x09
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
        irc_connect(host, port);
        s_cpy(S.server, host, sizeof(S.server));
        s_cpy(S.port, port, sizeof(S.port));
        save_settings();
    } else if (starts(cmd, "nick")) {
        if (arg[0]) { irc_set_nick(arg); save_settings(); }
    } else if (starts(cmd, "join")) {
        if (arg[0]) irc_join(arg);
    } else if (starts(cmd, "query")) {
        if (arg[0]) irc_query(arg); else term_notif("usage: /query <nick>");
    } else if (starts(cmd, "msg")) {
        char *target = arg, *text = next_arg(arg);
        if (!target[0] || !text[0]) { term_notif("usage: /msg <target> <text>"); return; }
        irc_msg(target, text);
    } else if (starts(cmd, "me")) {
        if (arg[0]) irc_me(arg); else term_notif("usage: /me <action>");
    } else if (starts(cmd, "part")) {
        irc_part();
    } else if (starts(cmd, "quit")) {
        irc_quit(); term_notif("disconnected");
    } else if (starts(cmd, "win")) {
        irc_next_window();
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
        irc_local("  /part                  - leave current channel");
        irc_local("  /quit                  - disconnect");
        irc_local("  /raw <text>            - send a raw IRC line");
        irc_local("  /ignore <nick>         - toggle ignoring a nick");
        irc_local("  /away [message]        - set/clear away");
        irc_local("  /timestamp (/ts)       - toggle message timestamps");
        irc_local("  /encoding (/enc)       - toggle UTF-8 <-> CP866");
        irc_local("  /whois /list /names... - any other /cmd is sent to the server");
        irc_local("  text                   - message current window");
        irc_local("  TAB window, arrows/Home/End edit, Up/Down hist, PgUp/PgDn scroll");
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
    cfg_load(&S);
    if (!S.nick[0]) { gen_nick(S.nick); cfg_save(&S); }   /* unique default nick, persisted */
    irc_init(S.nick);

    inlen = 0; incur = 0;
    eh_init();
    for (i = 0; i < IN_MAX; i++) inbuf[i] = 0;

    {
        i8 nr = net_init();
        if (nr == NET_NO_LINK) {            /* NETUP not run -> message and exit */
            dss_clrscr();
            dss_gotoxy(1, 1);
            dss_puts("SpecTalk ZX  --  Sprinter\r\n\r\n");
            dss_puts("Wi-Fi is not up (env NET != WIFI).\r\n");
            dss_puts("Run NETUP first to bring the network up,\r\n");
            dss_puts("then start SpecTalk again.\r\n\r\n");
            dss_puts("Press any key to exit.\r\n");
            dss_waitkey();
            dss_exit(1);
        }
        if (nr == NET_NO_HW) {
            irc_local("No SprinterWiFi (ESP) UART detected.");
            irc_local("UI works, networking unavailable.");
        } else {
        char m[40], *o = m;
        const char *p = "ESP ready. UART baud=";
        const char *b = net_cfg_baud();
        u8 d = net_cfg_div();
        while (*p) *o++ = *p++;
        if (*b) { while (*b) *o++ = *b++; } else { *o++ = '('; *o++ = 'd'; *o++ = 'e'; *o++ = 'f'; *o++ = ')'; }
        *o++ = ' '; *o++ = 'd'; *o++ = 'i'; *o++ = 'v'; *o++ = '=';
        *o++ = (char)('0' + (d / 100) % 10); *o++ = (char)('0' + (d / 10) % 10); *o++ = (char)('0' + d % 10);
        *o = 0;
        irc_local(m);
        irc_local("/help for commands.");
        }
    }

    if (S.loaded && S.server[0]) {           /* offer the last server via history */
        char seed[64], *o = seed;
        const char *p = "/server ";
        while (*p) *o++ = *p++;
        p = S.server; while (*p) *o++ = *p++;
        if (S.port[0]) { *o++ = ' '; p = S.port; while (*p) *o++ = *p++; }
        *o = 0;
        eh_seed(seed);
        irc_local("Saved server found: press Up to recall /server, ENTER to connect.");
    } else {
        irc_local("Try:  /server irc.libera.chat   then  /join #test");
    }
    term_notif("/help | /server <host> | /join #chan | TAB=window | ESC=exit");
    redraw();

    for (;;) {
        n = net_poll(rxb, sizeof(rxb));   /* drain UART first, before slower work */
        if (n) irc_feed(rxb, n);

        if (was_conn && !irc_connected()) {   /* link dropped (ESP reported CLOSED) */
            irc_on_disconnect();
            term_notif("Connection lost (server closed the link)");
        }
        was_conn = irc_connected();

        term_clock();

        if (dss_testkey(&key)) {
            dss_scankey(&consume);
            if (quit_pending) {
                quit_pending = 0;
                if (key.ascii == K_ESC) break;
                term_notif("quit cancelled");
                continue;
            }
            if (key.ascii == K_ESC) { quit_pending = 1; term_notif("Press ESC again to quit, any other key to cancel"); }
            else if (key.ascii == K_TAB || key.scan == 0x0F) {   /* Tab / Shift+Tab */
                if (key.modifiers & (DSS_KEYMOD_LSHIFT | DSS_KEYMOD_RSHIFT)) irc_prev_window();
                else irc_next_window();
                redraw();
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
            else if ((key.ascii >= 32 && key.ascii < 127) || key.ascii >= 0x80) { ins_char(key.ascii); redraw(); }
        }
    }

    if (irc_connected()) { term_notif("disconnecting, please wait..."); irc_quit(); }
    dss_clrscr();
    dss_gotoxy(1, 1);
    dss_puts("SpecTalk Sprinter - bye.\r\n");
    dss_exit(0);
}
