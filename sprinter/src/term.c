/*
 * term.c — terminal HAL implementation. See term.h.
 *
 * NOTE: dss_clear is 0-based; dss_gotoxy is 1-based. Public coordinates here are
 * 1-based (gotoxy convention); the clear wrapper subtracts 1 to match.
 *
 * Chat scrollback is kept in a RAM ring buffer and the chat region is redrawn on
 * each new line. (We avoid the DSS #55 hardware scroll: it wedged on a full-width
 * 80-col region in testing, and a RAM ring is what the IRC scrollback needs anyway.)
 */
#include "term.h"

#define ATTR_CURSOR 0x70    /* inverse block cursor (white paper, black ink) */
#define CHAT_W      80      /* stored width per chat line (79 chars + NUL) */

static char chat_buf[CHAT_H][CHAT_W];   /* NUL-terminated lines */
static u8   chat_n;         /* number of lines held (<= CHAT_H) */
static u8   chat_head;      /* ring index of the oldest visible line */

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

/* ---- chat ring ------------------------------------------------------ */

/* store a line in the ring; returns 1 if the ring scrolled (oldest dropped) */
static u8 chat_store(const char *s) {
    u8 idx, i, scrolled = 0;
    if (chat_n < CHAT_H) {
        idx = (u8)((chat_head + chat_n) % CHAT_H);
        chat_n++;
    } else {
        idx = chat_head;
        chat_head = (u8)((chat_head + 1) % CHAT_H);
        scrolled = 1;
    }
    for (i = 0; i < CHAT_W - 1 && s[i]; i++) chat_buf[idx][i] = s[i];
    chat_buf[idx][i] = 0;
    return scrolled;
}

/* draw one visible chat row (r = 0..chat_n-1). One clear + one PCHARS syscall. */
static void draw_chat_row(u8 r) {
    u8 idx = (u8)((chat_head + r) % CHAT_H);
    clear_rect(1, CHAT_TOP + r, SCR_W, 1, ATTR_NORMAL);
    dss_gotoxy(1, CHAT_TOP + r);
    dss_puts(chat_buf[idx]);
}

static void chat_redraw(void) {
    u8 r;
    for (r = 0; r < chat_n; r++) draw_chat_row(r);
}

/* ---- public --------------------------------------------------------- */

void term_init(void) {
    u8 i;
    dss_clrscr();
    clear_row(BANNER_ROW, ATTR_BANNER);
    clear_rect(1, CHAT_TOP, SCR_W, CHAT_H, ATTR_NORMAL);
    clear_row(SEP_ROW, ATTR_SEP);
    dss_gotoxy(1, SEP_ROW);
    for (i = 0; i < SCR_W - 1; i++) dss_putchar('-');  /* avoid wrap on last col */
    clear_row(STATUS_ROW, ATTR_STATUS);
    clear_row(NOTIF_ROW, ATTR_NOTIF);
    clear_row(INPUT_ROW, ATTR_NORMAL);
    chat_n = 0;
    chat_head = 0;
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

#define WRAP 78   /* max chars per chat row before wrapping */

static void add_one(const char *s) {
    if (chat_store(s)) chat_redraw();        /* scrolled: repaint all (cheap via PCHARS) */
    else draw_chat_row(chat_n - 1);          /* appended: draw only the new row */
}

/* Append a line, word-wrapping anything longer than WRAP onto further rows. */
void term_add_line(const char *s) {
    char chunk[WRAP + 1];
    u8 i, brk;
    for (;;) {
        /* does the remainder fit? */
        i = 0;
        while (s[i] && i < WRAP) i++;
        if (s[i] == 0) { add_one(s); return; }      /* fits on one row */
        /* break at the last space within WRAP, else hard-break at WRAP */
        brk = WRAP;
        while (brk > 0 && s[brk] != ' ') brk--;
        if (brk == 0) brk = WRAP;
        for (i = 0; i < brk; i++) chunk[i] = s[i];
        chunk[i] = 0;
        add_one(chunk);
        s += brk;
        while (*s == ' ') s++;                        /* drop the break space(s) */
        if (*s == 0) return;
    }
}

void term_clear_chat(void) {
    clear_rect(1, CHAT_TOP, SCR_W, CHAT_H, ATTR_NORMAL);
    chat_n = 0;
    chat_head = 0;
}

void term_status(const char *s) {
    clear_row(STATUS_ROW, ATTR_STATUS);
    put_clip(2, STATUS_ROW, s);
}

void term_notif(const char *s) {
    clear_row(NOTIF_ROW, ATTR_NOTIF);
    put_clip(2, NOTIF_ROW, s);
}

void term_input(const char *buf, u8 len, u8 cur) {
    u8 i, col, ch;
    clear_row(INPUT_ROW, ATTR_NORMAL);
    dss_gotoxy(1, INPUT_ROW);
    dss_putchar('>'); dss_putchar(' ');
    for (i = 0; i < len && i < (SCR_W - 3); i++) dss_putchar((u8)buf[i]);
    col = 3 + cur;
    if (col > SCR_W) col = SCR_W;
    ch = (cur < len) ? (u8)buf[cur] : ' ';
    dss_clear(col - 1, INPUT_ROW - 1, 1, 1, ATTR_CURSOR, ch);   /* 0-based */
}
