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

#define IN_MAX  120
#define HIST_N  8

/* ---- small string helpers ------------------------------------------ */
static u8 starts(const char *s, const char *p) { while (*p) { if (*s != *p) return 0; s++; p++; } return 1; }

/* ---- editor state -------------------------------------------------- */
static char inbuf[IN_MAX];
static u8   inlen, incur;
static char hist[HIST_N][IN_MAX];
static u8   histl[HIST_N], hcount, hbrowse;
static u8   rxb[256];

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
    } else if (starts(cmd, "nick")) {
        if (arg[0]) irc_set_nick(arg);
    } else if (starts(cmd, "join")) {
        if (arg[0]) irc_join(arg);
    } else if (starts(cmd, "part")) {
        irc_part();
    } else if (starts(cmd, "quit")) {
        irc_quit(); term_notif("disconnected");
    } else if (starts(cmd, "win")) {
        irc_next_window();
    } else if (starts(cmd, "raw")) {
        irc_raw(arg);
    } else if (starts(cmd, "help")) {
        term_add_line("Commands:");
        term_add_line("  /server <host> [port]  - connect (default 6667)");
        term_add_line("  /nick <name>           - change nick");
        term_add_line("  /join #channel         - join (opens a window)");
        term_add_line("  /part                  - leave current channel");
        term_add_line("  /quit                  - disconnect");
        term_add_line("  /raw <text>            - send a raw IRC line");
        term_add_line("  text                   - message current window");
        term_add_line("  TAB switch window, arrows/Home/End edit, Up/Down hist");
    } else {
        term_notif("unknown command (try /help)");
    }
}

static void on_enter(void) {
    inbuf[inlen] = 0;
    if (inlen == 0) return;
    hist_push();
    if (inbuf[0] == '/') do_command(inbuf);
    else irc_say(inbuf);
    inlen = 0; incur = 0;
}

/* ---- main ---------------------------------------------------------- */
void main(void) {
    dss_key_t key, consume;
    u16 i, n;
    u8 c, quit_pending = 0;

    term_init();
    irc_init("SprSpecTalk");

    inlen = 0; incur = 0; hcount = 0; hbrowse = 0;
    for (i = 0; i < IN_MAX; i++) inbuf[i] = 0;
    for (c = 0; c < HIST_N; c++) histl[c] = 0;

    if (net_init() == NET_NO_HW) {
        term_add_line("No SprinterWiFi (ESP) UART detected.");
        term_add_line("UI works, networking unavailable.");
    } else {
        term_add_line("ESP ready. Run NETUP first if Wi-Fi is not up.");
        term_add_line("Try:  /server irc.libera.chat   then  /join #test");
        term_add_line("/help for commands.");
    }
    term_notif("/help | /server <host> | /join #chan | TAB=window | ESC=exit");
    redraw();

    for (;;) {
        term_clock();

        n = net_poll(rxb, sizeof(rxb));
        if (n) irc_feed(rxb, n);

        if (dss_testkey(&key)) {
            dss_scankey(&consume);
            if (quit_pending) {
                quit_pending = 0;
                if (key.ascii == K_ESC) break;
                term_notif("quit cancelled");
                continue;
            }
            if (key.ascii == K_ESC) { quit_pending = 1; term_notif("Press ESC again to quit, any other key to cancel"); }
            else if (key.ascii == K_TAB) { irc_next_window(); redraw(); }
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

    if (irc_connected()) { term_notif("disconnecting, please wait..."); irc_quit(); }
    dss_clrscr();
    dss_gotoxy(1, 1);
    dss_puts("SpecTalk Sprinter - bye.\r\n");
    dss_exit(0);
}
