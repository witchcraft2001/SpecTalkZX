/*
 * SpecTalk ZX — Sprinter DSS port
 * Stage 1: HAL smoke test.
 *
 * Proves the foundation every later stage builds on:
 *   - 80x32 text mode (DSS video mode #03)
 *   - full-screen border to confirm the screen extent
 *   - live clock via DSS SYSTIME
 *   - non-blocking keyboard polling via dss_scankey (the main-loop input model)
 *   - clean exit back to DSS
 *
 * Copyright (C) 2026 M. Ignacio Monge Garcia — GPLv2 (see ../../LICENSE)
 */

#include <sprinter.h>

#define STAGE   "Stage 1: HAL smoke test"
#define VERSION "1.3.7 -> Sprinter port"

#define COLS 80
#define ROWS 32

/* Layout rows (1-based, DSS gotoxy convention) */
#define ROW_TITLE   2
#define ROW_VER     3
#define ROW_MODE    5
#define ROW_CLOCK   6
#define ROW_HELP    8
#define ROW_KEYDBG  10
#define ROW_INPUTL  12   /* label */
#define ROW_INPUT   13   /* echoed input line */

/* Keys */
#define K_ESC   27
#define K_ENTER 13
#define K_BS1   8
#define K_BS2   12

static char inbuf[COLS];   /* echoed input characters */
static u8   inlen;

/* ---- tiny formatting helpers ---------------------------------------- */

static void put_at(u8 x, u8 y, const char *s) {
    dss_gotoxy(x, y);
    dss_puts(s);
}

static void put2(u8 v) {           /* zero-padded 2 digits */
    dss_putchar('0' + (v / 10) % 10);
    dss_putchar('0' + v % 10);
}

static void put4(u16 v) {          /* zero-padded 4 digits */
    dss_putchar('0' + (u8)((v / 1000) % 10));
    dss_putchar('0' + (u8)((v / 100) % 10));
    dss_putchar('0' + (u8)((v / 10) % 10));
    dss_putchar('0' + (u8)(v % 10));
}

static void put_hex8(u8 v) {
    static const char hx[] = "0123456789ABCDEF";
    dss_putchar(hx[(v >> 4) & 0x0F]);
    dss_putchar(hx[v & 0x0F]);
}

/* ---- screen --------------------------------------------------------- */

static void draw_border(void) {
    u8 x, y;
    for (x = 1; x <= COLS; x++) {
        dss_gotoxy(x, 1);    dss_putchar('=');
        dss_gotoxy(x, ROWS); dss_putchar('=');
    }
    for (y = 2; y < ROWS; y++) {
        dss_gotoxy(1, y);    dss_putchar('|');
        dss_gotoxy(COLS, y); dss_putchar('|');
    }
    /* corner ticks so the exact 80x32 extent is unmistakable */
    dss_gotoxy(1, 1);       dss_putchar('+');
    dss_gotoxy(COLS, 1);    dss_putchar('+');
    dss_gotoxy(1, ROWS);    dss_putchar('+');
    dss_gotoxy(COLS, ROWS); dss_putchar('+');
}

static void draw_static(void) {
    put_at(4, ROW_TITLE, "SpecTalk ZX  ::  Sprinter DSS port");
    put_at(4, ROW_VER,   VERSION "   " STAGE);
    put_at(4, ROW_MODE,  "Video mode : 80x32 text (DSS #03)  -- border marks the full screen");
    put_at(4, ROW_HELP,  "Type to echo below. ENTER clears. BACKSPACE deletes. ESC exits to DSS.");
    put_at(4, ROW_INPUTL,"Input >");
}

static void draw_clock(dss_date_t *d, dss_time_t *t) {
    dss_gotoxy(4, ROW_CLOCK);
    dss_puts("Clock      : ");
    put4(d->year); dss_putchar('-'); put2(d->month); dss_putchar('-'); put2(d->day);
    dss_puts("  ");
    put2(t->hour); dss_putchar(':'); put2(t->minute); dss_putchar(':'); put2(t->second);
}

static void draw_keydbg(dss_key_t *k) {
    dss_gotoxy(4, ROW_KEYDBG);
    dss_puts("Last key   : ascii=0x"); put_hex8(k->ascii);
    dss_puts(" scan=0x");              put_hex8(k->scan);
    dss_puts(" mod=0x");               put_hex8(k->modifiers);
    dss_puts("   ");
}

static void draw_input(void) {
    u8 i;
    dss_gotoxy(12, ROW_INPUT);
    for (i = 0; i < inlen; i++) dss_putchar(inbuf[i]);
    /* clear the tail of the line (within the border) */
    for (; i < COLS - 13; i++) dss_putchar(' ');
}

/* ---- main ----------------------------------------------------------- */

void main(void) {
    dss_key_t  key;
    dss_date_t date;
    dss_time_t time;
    u8 last_sec = 0xFF;

    dss_setvmod(VMODE_TEXT80, 0);   /* 80x32 text */
    dss_clrscr();

    draw_border();
    draw_static();
    inlen = 0;
    draw_input();

    for (;;) {
        /* live clock — redraw only when the second changes (no flicker) */
        dss_gettime(&time);
        if (time.second != last_sec) {
            dss_getdate(&date);
            draw_clock(&date, &time);
            last_sec = time.second;
        }

        if (dss_scankey(&key)) {
            draw_keydbg(&key);

            if (key.ascii == K_ESC) {
                break;
            } else if (key.ascii == K_ENTER) {
                inlen = 0;
                draw_input();
            } else if (key.ascii == K_BS1 || key.ascii == K_BS2) {
                if (inlen > 0) { inlen--; draw_input(); }
            } else if (key.ascii >= 32 && key.ascii < 127) {
                if (inlen < (COLS - 14)) { inbuf[inlen++] = key.ascii; draw_input(); }
            }
        }
    }

    dss_clrscr();
    dss_gotoxy(1, 1);
    dss_puts("SpecTalk Sprinter port - Stage 1 OK. Bye.\r\n");
    dss_exit(0);
}
