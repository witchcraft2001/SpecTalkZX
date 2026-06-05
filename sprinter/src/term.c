/*
 * term.c — terminal HAL implementation. See term.h.
 *
 * NOTE: dss_clear is 0-based; dss_gotoxy is 1-based. Public coordinates here are
 * 1-based (gotoxy convention); the clear wrapper subtracts 1 to match.
 *
 * The chat area has no RAM mirror: all chat content lives in the per-window DSS
 * history pages (hist.c) and is drawn straight to a row via term_draw_row.
 */
#include "term.h"

#define ATTR_CURSOR 0x70    /* inverse block cursor (white paper, black ink) */

/* ---- low-level helpers (1-based in, converted to 0-based for DSS) --- */

static void clear_rect(u8 x, u8 y, u8 w, u8 h, u8 attr) {
    dss_clear(x - 1, y - 1, w, h, attr, ' ');
}

static void clear_row(u8 y, u8 attr) {
    clear_rect(1, y, SCR_W, 1, attr);
}

static void put_clip(u8 x, u8 y, const char *s) {
    u8 room = (u8)(SCR_W - (x - 1));
    dss_gotoxy(x, y);
    while (*s && room) { dss_putchar((u8)*s++); room--; }
}

static void put2(u8 v) {
    dss_putchar('0' + (v / 10) % 10);
    dss_putchar('0' + v % 10);
}

/* ---- public --------------------------------------------------------- */

void term_init(void) {
    dss_clrscr();
    clear_row(BANNER_ROW, ATTR_BANNER);
    clear_rect(1, CHAT_TOP, SCR_W, CHAT_H, ATTR_NORMAL);
    clear_row(STATUS_ROW, ATTR_STATUS);   /* inverse bar doubles as the separator */
    clear_row(NOTIF_ROW, ATTR_NOTIF);
    clear_row(INPUT_ROW, ATTR_NORMAL);
}

void term_banner(const char *title) {
    clear_row(BANNER_ROW, ATTR_BANNER);
    put_clip(2, BANNER_ROW, title);
}

void term_clock(void) {
    dss_time_t t;
    dss_gettime(&t);
    dss_gotoxy(SCR_W - 8, BANNER_ROW);
    put2(t.hour); dss_putchar(':');
    put2(t.minute); dss_putchar(':');
    put2(t.second);
}

/* draw one chat row (0..CHAT_H-1): one clear + one PCHARS syscall */
void term_draw_row(u8 r, const char *s) {
    clear_rect(1, CHAT_TOP + r, SCR_W, 1, ATTR_NORMAL);
    dss_gotoxy(1, CHAT_TOP + r);
    dss_puts(s);
}

void term_clear_chat(void) {
    clear_rect(1, CHAT_TOP, SCR_W, CHAT_H, ATTR_NORMAL);
}

/* Scroll the chat region up one line via BIOS #8A (LP_SCROLL_UD).
 * Per the BIOS source: B=direction(1=up), D=TOP row (0-based), E=row COUNT.
 * The routine remaps VRAM pages and does NOT disable interrupts itself, so we
 * wrap it in di/ei. Bottom row is left for the caller to redraw.
 * Keep D/E in sync with term.h: CHAT_TOP=2 (0-based 1), CHAT_H=28. */
void term_scroll_chat(void) __naked {
    __asm
        push    ix
        di
        ld      b, #0x01        ; up
        ld      d, #0x01        ; top row, 0-based = CHAT_TOP-1
        ld      e, #0x1C        ; CHAT_H = 28 rows
        ld      c, #0x8A        ; BIOS LP_SCROLL_UD
        rst     #0x08
        ei
        pop     ix
        ret
    __endasm;
}

void term_status(const char *s) {
    clear_row(STATUS_ROW, ATTR_STATUS);
    put_clip(2, STATUS_ROW, s);
}

void term_notif(const char *s) {
    clear_row(NOTIF_ROW, ATTR_NOTIF);
    put_clip(2, NOTIF_ROW, s);
}

void term_input(const char *buf, u16 len, u16 cur) {
    /* Horizontal scroll: input can be longer than the 77-col visible field, so
     * slide a window that keeps the cursor on screen. '<' marks hidden text left. */
    u8 i, col, ch, vw = SCR_W - 3;     /* visible chars after "> " */
    u16 off = (cur >= vw) ? (u16)(cur - vw + 1) : 0;
    clear_row(INPUT_ROW, ATTR_NORMAL);
    dss_gotoxy(1, INPUT_ROW);
    dss_putchar(off ? '<' : '>'); dss_putchar(' ');
    for (i = 0; i < vw && (off + i) < len; i++) dss_putchar((u8)buf[off + i]);
    col = (u8)(3 + (cur - off));
    if (col > SCR_W) col = SCR_W;
    ch = (cur < len) ? (u8)buf[cur] : ' ';
    dss_clear(col - 1, INPUT_ROW - 1, 1, 1, ATTR_CURSOR, ch);   /* 0-based */
}
