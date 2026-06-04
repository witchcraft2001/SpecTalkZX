/*
 * SpecTalk ZX — Sprinter DSS port
 * Stage 2: terminal HAL + IRC-style 80x32 layout + input editor.
 *
 *   - banner (title + live clock), scrollback chat area (DSS #55 scroll),
 *     inverse status bar, notification line
 *   - line editor: insert/delete at cursor, LEFT/RIGHT/HOME/END cursor movement,
 *     UP/DOWN command history, block cursor, CP866 input
 *
 * Copyright (C) 2026 M. Ignacio Monge Garcia — GPLv2 (see ../../LICENSE)
 */

#include "term.h"

/* ASCII keys */
#define K_ESC   0x1B
#define K_ENTER 0x0D
#define K_BS    0x08

/* scan codes for navigation keys (these carry no ASCII) */
#define SC_LEFT  0x54
#define SC_RIGHT 0x56
#define SC_UP    0x58
#define SC_DOWN  0x52
#define SC_HOME  0x57
#define SC_END   0x51

#define IN_MAX  72
#define HIST_N  8

static char inbuf[IN_MAX];
static u8   inlen;
static u8   incur;          /* cursor index 0..inlen */

static char hist[HIST_N][IN_MAX];
static u8   histl[HIST_N];   /* length of each stored line */
static u8   hcount;          /* number of stored lines (<= HIST_N) */
static u8   hbrowse;         /* 0..hcount; == hcount means "fresh line" */

/* ---- input editing -------------------------------------------------- */

static void redraw(void) { term_input(inbuf, inlen, incur); }

static void ins_char(u8 c) {
    u8 i;
    if (inlen >= IN_MAX) return;
    if (incur > inlen) incur = inlen;            /* defensive */
    for (i = inlen; i > incur; i--) inbuf[i] = inbuf[i - 1];
    inbuf[incur] = (char)c;
    inlen++; incur++;
}

static void del_before(void) {
    u8 i;
    if (incur == 0) return;
    for (i = incur - 1; i < inlen - 1; i++) inbuf[i] = inbuf[i + 1];
    inlen--; incur--;
}

static void load_from(const char *src, u8 len) {
    u8 i;
    if (len > IN_MAX) len = IN_MAX;          /* defensive clamp */
    for (i = 0; i < len; i++) inbuf[i] = src[i];
    inlen = len; incur = len;
}

static void hist_push(void) {
    u8 i;
    if (hcount == HIST_N) {              /* drop oldest, shift down */
        u8 r;
        for (r = 1; r < HIST_N; r++) {
            for (i = 0; i < histl[r]; i++) hist[r - 1][i] = hist[r][i];
            histl[r - 1] = histl[r];
        }
        hcount = HIST_N - 1;
    }
    for (i = 0; i < inlen; i++) hist[hcount][i] = inbuf[i];
    histl[hcount] = inlen;
    hcount++;
    hbrowse = hcount;
}

static void hist_up(void) {
    if (hbrowse == 0) return;
    hbrowse--;
    load_from(hist[hbrowse], histl[hbrowse]);
    redraw();
}

static void hist_down(void) {
    if (hbrowse >= hcount) return;
    hbrowse++;
    if (hbrowse == hcount) { inlen = 0; incur = 0; }
    else load_from(hist[hbrowse], histl[hbrowse]);
    redraw();
}

/* ---- key-code readout for still-unbound keys ------------------------ */

static void put_hex8(char *o, u8 v) {
    static const char hx[] = "0123456789ABCDEF";
    o[0] = hx[(v >> 4) & 0x0F];
    o[1] = hx[v & 0x0F];
}

static void show_keycode(dss_key_t *k) {
    static char msg[48] = "unbound key  ascii=0x.. scan=0x.. mod=0x..";
    put_hex8(&msg[21], k->ascii);
    put_hex8(&msg[31], k->scan);
    put_hex8(&msg[40], k->modifiers);
    term_notif(msg);
}

/* ---- main ----------------------------------------------------------- */

void main(void) {
    dss_key_t key, consume;
    char line[88];
    u8 i;

    term_init();
    term_banner("SpecTalk ZX  ::  Sprinter port  --  Stage 2: terminal HAL");
    term_clock();
    term_status("guest   offline   no channel   (Stage 2 demo)");
    term_notif("ENTER posts. arrows/Home/End move. UP/DOWN history. ESC quits.");

    for (i = 1; i <= CHAT_H; i++) {
        char seed[20] = "seed line ..";
        seed[10] = '0' + (i / 10);
        seed[11] = '0' + (i % 10);
        term_add_line(seed);
    }

    inlen = 0; incur = 0;
    hcount = 0; hbrowse = 0;
    for (i = 0; i < IN_MAX; i++) inbuf[i] = 0;    /* _DATA is not zeroed by crt0 */
    for (i = 0; i < HIST_N; i++) histl[i] = 0;
    redraw();

    for (;;) {
        term_clock();

        if (dss_testkey(&key)) {
            dss_scankey(&consume);

            if (key.ascii == K_ESC) {
                break;
            } else if (key.ascii == K_ENTER) {
                if (inlen > 0) {
                    char *o = line;
                    const char *p = "guest> ";
                    while (*p) *o++ = *p++;
                    for (i = 0; i < inlen; i++) *o++ = inbuf[i];
                    *o = 0;
                    term_add_line(line);
                    hist_push();
                    inlen = 0; incur = 0;
                    redraw();
                }
            } else if (key.ascii == K_BS) {
                del_before(); redraw();
            } else if (key.scan == SC_LEFT)  { if (incur > 0)     { incur--; redraw(); } }
            else if (key.scan == SC_RIGHT) { if (incur < inlen) { incur++; redraw(); } }
            else if (key.scan == SC_HOME)  { incur = 0; redraw(); }
            else if (key.scan == SC_END)   { incur = inlen; redraw(); }
            else if (key.scan == SC_UP)    { hist_up(); }
            else if (key.scan == SC_DOWN)  { hist_down(); }
            else if ((key.ascii >= 32 && key.ascii < 127) || key.ascii >= 0x80) {
                ins_char(key.ascii); redraw();
            } else {
                show_keycode(&key);
            }
        }
    }

    dss_clrscr();
    dss_gotoxy(1, 1);
    dss_puts("SpecTalk Sprinter port - Stage 2 OK. Bye.\r\n");
    dss_exit(0);
}
