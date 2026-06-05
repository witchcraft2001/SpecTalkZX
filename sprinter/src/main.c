/*
 * SpecTalk ZX — Sprinter DSS port
 * Stage 5a: minimal live IRC client.
 *
 * Chat UI (term.c) + ESP transport (net.h) + a minimal IRC line parser.
 * Commands: /server <host> [port], /nick <name>, /join #chan, /part, /quit,
 * /raw <text>. Plain text -> PRIVMSG to the current channel. PING is auto-PONGed.
 *
 * This is the "minimal live client" milestone; the full irc_handlers.c logic and
 * multi-window support are adapted in on top of this (see TODO.md).
 *
 * Copyright (C) 2026 M. Ignacio Monge Garcia — GPLv2 (see ../../LICENSE)
 */

#include "term.h"
#include "net.h"

/* keys */
#define K_ESC   0x1B
#define K_ENTER 0x0D
#define K_BS    0x08
#define SC_LEFT  0x54
#define SC_RIGHT 0x56
#define SC_UP    0x58
#define SC_DOWN  0x52
#define SC_HOME  0x57
#define SC_END   0x51

#define IN_MAX  120
#define HIST_N  8

/* ---- small string helpers ------------------------------------------ */
static u8 s_len(const char *s) { u8 n = 0; while (s[n]) n++; return n; }
static void s_cpy(char *d, const char *s, u8 max) {
    u8 i = 0; while (s[i] && i < (u8)(max - 1)) { d[i] = s[i]; i++; } d[i] = 0;
}
static u8 starts(const char *s, const char *p) {     /* s starts with p? */
    while (*p) { if (*s != *p) return 0; s++; p++; } return 1;
}

/* ---- editor state -------------------------------------------------- */
static char inbuf[IN_MAX];
static u8   inlen, incur;
static char hist[HIST_N][IN_MAX];
static u8   histl[HIST_N], hcount, hbrowse;

/* ---- session state ------------------------------------------------- */
static char nick[20] = "SprSpecTalk";
static char chan[34];           /* current channel ("" = none) */
static char echo[160];          /* local-echo / status scratch */

/* ---- server line assembly ------------------------------------------ */
static char srv[300];
static u16  srvpos;
static u8   rxb[256];

/* ---- status bar ---------------------------------------------------- */
static void show_status(void) {
    char *o = echo;
    const char *st = net_is_connected() ? "online " : "offline";
    while (*st) *o++ = *st++;
    { const char *p = "   nick:"; while (*p) *o++ = *p++; }
    { const char *p = nick; while (*p) *o++ = *p++; }
    { const char *p = "   chan:"; while (*p) *o++ = *p++; }
    { const char *p = chan[0] ? chan : "(none)"; while (*p) *o++ = *p++; }
    *o = 0;
    term_status(echo);
}

/* ---- input editor (cursor move, history) --------------------------- */
static void redraw(void) { term_input(inbuf, inlen, incur); }

static void ins_char(u8 c) {
    u8 i;
    if (inlen >= IN_MAX - 1) return;
    if (incur > inlen) incur = inlen;
    for (i = inlen; i > incur; i--) inbuf[i] = inbuf[i - 1];
    inbuf[incur] = (char)c; inlen++; incur++;
}
static void del_before(void) {
    u8 i;
    if (incur == 0) return;
    for (i = incur - 1; i < inlen - 1; i++) inbuf[i] = inbuf[i + 1];
    inlen--; incur--;
}
static void hist_load(const char *src, u8 len) {
    u8 i; for (i = 0; i < len; i++) inbuf[i] = src[i]; inlen = len; incur = len;
}
static void hist_push(void) {
    u8 i;
    if (inlen == 0) return;
    if (hcount == HIST_N) {
        u8 r; for (r = 1; r < HIST_N; r++) {
            for (i = 0; i < histl[r]; i++) hist[r - 1][i] = hist[r][i];
            histl[r - 1] = histl[r];
        }
        hcount = HIST_N - 1;
    }
    for (i = 0; i < inlen; i++) hist[hcount][i] = inbuf[i];
    histl[hcount] = inlen; hcount++; hbrowse = hcount;
}

/* ---- IRC output ---------------------------------------------------- */
static void irc_register(void) {
    net_send("NICK "); net_send(nick); net_send("\r\n");
    net_send("USER spectalk 0 * :SpecTalk ZX Sprinter\r\n");
}

/* add "<who> text" to the chat */
static void add_msg(const char *who, const char *text) {
    char *o = echo;
    *o++ = '<'; { const char *p = who; while (*p && o < echo + 30) *o++ = *p++; } *o++ = '>'; *o++ = ' ';
    { const char *p = text; while (*p && o < echo + sizeof(echo) - 1) *o++ = *p++; }
    *o = 0;
    term_add_line(echo);
}

/* ---- incoming server line ------------------------------------------ */
static void on_server_line(char *s) {
    char *p, *who, *text, *cmd;
    u16 L = 0;
    while (s[L]) L++;
    if (L && s[L - 1] == '\r') s[L - 1] = 0;    /* strip trailing CR */

    if (starts(s, "PING")) {                    /* PING :token -> PONG :token */
        net_send("PONG"); net_send(s + 4); net_send("\r\n");
        return;
    }

    if (s[0] == ':') {                          /* :prefix CMD args... */
        who = s + 1;
        cmd = s; while (*cmd && *cmd != ' ') cmd++;   /* end of prefix */
        while (*cmd == ' ') cmd++;                    /* -> command word */
        if (starts(cmd, "PRIVMSG")) {
            text = cmd;                                /* text after " :" */
            while (*text && !(text[0] == ' ' && text[1] == ':')) text++;
            if (*text) text += 2;
            p = who; while (*p && *p != '!' && *p != ' ') p++; *p = 0;  /* nick */
            add_msg(who, text);
            return;
        }
    }
    term_add_line(s);                            /* show everything else raw */
}

/* ---- command handling ---------------------------------------------- */
static char *next_arg(char *s) {               /* skip a token + spaces, return rest */
    while (*s && *s != ' ') s++;
    while (*s == ' ') *s++ = 0;                 /* NUL-terminate the token, skip spaces */
    return s;
}

static void do_command(char *line) {
    char *cmd = line + 1;                       /* skip '/' */
    char *arg = next_arg(cmd);

    if (starts(cmd, "server")) {
        char *host = arg;
        char *port = next_arg(arg);
        if (!host[0]) { term_notif("usage: /server <host> [port]"); return; }
        if (!port[0]) port = "6667";
        add_msg("*", "connecting...");
        if (net_connect(host, port) == NET_OK) {
            irc_register();
            term_notif("connected; registering...");
        } else term_notif("connect failed");
        show_status();
    } else if (starts(cmd, "nick")) {
        if (arg[0]) { s_cpy(nick, arg, sizeof(nick));
            if (net_is_connected()) { net_send("NICK "); net_send(nick); net_send("\r\n"); }
            show_status();
        }
    } else if (starts(cmd, "join")) {
        if (arg[0]) { s_cpy(chan, arg, sizeof(chan));
            net_send("JOIN "); net_send(chan); net_send("\r\n"); show_status();
        }
    } else if (starts(cmd, "part")) {
        if (chan[0]) { net_send("PART "); net_send(chan); net_send("\r\n"); chan[0] = 0; show_status(); }
    } else if (starts(cmd, "quit")) {
        net_send("QUIT :SpecTalk ZX\r\n"); net_close(); chan[0] = 0; show_status();
        term_notif("disconnected");
    } else if (starts(cmd, "raw")) {
        net_send(arg); net_send("\r\n");
    } else {
        term_notif("unknown command");
    }
}

static void on_enter(void) {
    inbuf[inlen] = 0;
    if (inlen == 0) return;
    hist_push();
    if (inbuf[0] == '/') {
        do_command(inbuf);
    } else if (chan[0] && net_is_connected()) {
        net_send("PRIVMSG "); net_send(chan); net_send(" :"); net_send(inbuf); net_send("\r\n");
        add_msg(nick, inbuf);
    } else {
        term_notif("not in a channel - use /server then /join");
    }
    inlen = 0; incur = 0;
}

/* ---- main ---------------------------------------------------------- */
void main(void) {
    dss_key_t key, consume;
    u16 i, n;
    u8 c;

    term_init();
    term_banner("SpecTalk ZX  ::  Sprinter port  --  live IRC client");
    term_clock();

    inlen = 0; incur = 0; hcount = 0; hbrowse = 0; chan[0] = 0; srvpos = 0;
    for (i = 0; i < IN_MAX; i++) inbuf[i] = 0;
    for (c = 0; c < HIST_N; c++) histl[c] = 0;

    if (net_init() == NET_NO_HW) {
        term_add_line("No SprinterWiFi (ESP) UART detected.");
        term_add_line("The UI works, but networking is unavailable.");
    } else {
        term_add_line("ESP ready. Run NETUP first if Wi-Fi is not up.");
        term_add_line("Try:  /server irc.libera.chat   then  /join #test");
    }
    show_status();
    term_notif("/server <host> [port] | /nick | /join #chan | /quit | ESC=exit");
    redraw();

    for (;;) {
        term_clock();

        /* network -> chat */
        n = net_poll(rxb, sizeof(rxb));
        for (i = 0; i < n; i++) {
            c = rxb[i];
            if (c == '\n') { srv[srvpos] = 0; on_server_line(srv); srvpos = 0; }
            else if (srvpos < sizeof(srv) - 1) srv[srvpos++] = c;
        }

        /* keyboard -> editor */
        if (dss_testkey(&key)) {
            dss_scankey(&consume);
            if (key.ascii == K_ESC) break;
            else if (key.ascii == K_ENTER) { on_enter(); redraw(); }
            else if (key.ascii == K_BS) { del_before(); redraw(); }
            else if (key.scan == SC_LEFT)  { if (incur > 0) { incur--; redraw(); } }
            else if (key.scan == SC_RIGHT) { if (incur < inlen) { incur++; redraw(); } }
            else if (key.scan == SC_HOME)  { incur = 0; redraw(); }
            else if (key.scan == SC_END)   { incur = inlen; redraw(); }
            else if (key.scan == SC_UP)    { if (hbrowse > 0) { hbrowse--; hist_load(hist[hbrowse], histl[hbrowse]); redraw(); } }
            else if (key.scan == SC_DOWN)  { if (hbrowse < hcount) { hbrowse++; if (hbrowse == hcount) { inlen = 0; incur = 0; } else hist_load(hist[hbrowse], histl[hbrowse]); redraw(); } }
            else if ((key.ascii >= 32 && key.ascii < 127) || key.ascii >= 0x80) { ins_char(key.ascii); redraw(); }
        }
    }

    if (net_is_connected()) { net_send("QUIT :bye\r\n"); net_close(); }
    dss_clrscr();
    dss_gotoxy(1, 1);
    dss_puts("SpecTalk Sprinter - bye.\r\n");
    dss_exit(0);
}
