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

/* One row's worth of text, assembled here and pushed with a single dss_puts.
 * Every DSS call costs a trip through the win0 RST trampoline, so a row drawn
 * character by character is eighty syscalls; the input row is redrawn on every
 * keystroke, which is where that showed up as typing lag. */
static char rowbuf[SCR_W + 1];

static void put_clip(u8 x, u8 y, const char *s) {
    u8 room = (u8)(SCR_W - (x - 1)), i = 0;
    while (*s && room) { rowbuf[i++] = *s++; room--; }
    rowbuf[i] = 0;
    dss_gotoxy(x, y);
    dss_puts(rowbuf);
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
    static u8 last = 0xFF;
    dss_time_t t;
    u8 sec, prev;
    dss_gettime(&t);
    /* Store BEFORE the comparison. SDCC 4.5 compiles the natural
     *     if (t.second == last) return; last = t.second;
     * into `sub a,(hl) / jr Z / ld (last),a` -- it believes A still holds
     * t.second after the SUB, so `last` ends up holding the DIFFERENCE and
     * never matches again. The clock then repainted on every main-loop pass:
     * a gotoxy, eight dss_putchar and six software divisions per pass, which
     * was most of the client's idle CPU. Writing first keeps the store on a
     * value that is provably live. See PLATFORM.md. */
    sec = t.second;
    prev = last;
    last = sec;
    if (sec == prev) return;               /* redraw only when the second changes */
    dss_gotoxy(SCR_W - 8, BANNER_ROW);
    put2(t.hour); dss_putchar(':');
    put2(t.minute); dss_putchar(':');
    put2(t.second);
}

/* Per-nick colour: hash the nick to a readable ink on black paper. */
static u8 nick_attr(const char *a, const char *b) {
    static const u8 pal[] = { 0x0A, 0x0E, 0x0D, 0x0B, 0x0C, 0x09, 0x06, 0x0F };
    u16 h = 0;
    while (a < b) { h = (u16)(h * 31u + (u8)*a); a++; }
    return pal[h & 7];
}

/* draw one chat row (0..CHAT_H-1): clear, recolour the nick span, print line.
 * dss_puts writes chars but not attributes, so we pre-set the nick cells' colour
 * and the text drops onto them. Nick = leading "<nick>" or "-nick-" (after an
 * optional "[HH:MM] " timestamp); everything else stays ATTR_NORMAL. */
void term_draw_row(u8 r, const char *s) {
    u8 y = CHAT_TOP + r;
    const char *q = s, *a = 0, *b = 0;
    if (s[0] == '[' && s[6] == ']' && s[7] == ' ') q = s + 8;   /* skip timestamp */
    if (q[0] == '<')      { a = q + 1; b = a; while (*b && *b != '>') b++; }
    else if (q[0] == '-') { a = q + 1; b = a; while (*b && *b != '-') b++; }

    clear_rect(1, y, SCR_W, 1, ATTR_NORMAL);
    if (a && b > a && *b) {
        u8 x0 = (u8)(1 + (a - s)), len = (u8)(b - a);
        if (x0 >= 1 && (u16)x0 + len <= SCR_W) clear_rect(x0, y, len, 1, nick_attr(a, b));
    }
    dss_gotoxy(1, y);
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
    rowbuf[0] = off ? '<' : '>';
    rowbuf[1] = ' ';
    for (i = 0; i < vw && (off + i) < len; i++) rowbuf[2 + i] = buf[off + i];
    rowbuf[2 + i] = 0;
    dss_gotoxy(1, INPUT_ROW);
    dss_puts(rowbuf);
    col = (u8)(3 + (cur - off));
    if (col > SCR_W) col = SCR_W;
    ch = (cur < len) ? (u8)buf[cur] : ' ';
    dss_clear(col - 1, INPUT_ROW - 1, 1, 1, ATTR_CURSOR, ch);   /* 0-based */
}
