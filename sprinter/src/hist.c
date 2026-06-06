/*
 * hist.c — per-window scrollback in DSS pages. See hist.h.
 */
#include "hist.h"
#include "term.h"

#define MAX_WIN  11              /* must match irc.h */
#define WIN3     0xC000          /* page maps here */
#define SLOT     80             /* bytes per stored line */
#define NSLOTS   204            /* 16384 / 80 */
#define WRAP     78
#define NO_PAGE  0xFF

typedef struct {
    u8  page;       /* DSS block id, or NO_PAGE */
    u16 lines;      /* lines currently stored (0..NSLOTS) */
    u16 first;      /* ring index of the oldest line */
    i16 view;       /* top visible logical line, or -1 = live (bottom) */
    u8  shown;      /* rows currently drawn (for incremental live append) */
} Hist;

static Hist H[MAX_WIN];
static char tmp[SLOT];          /* WIN2 staging buffer (copy here before any DSS call) */

/* ---- DSS paging (declared in dss.h via sprinter.h through term.h) --- */
extern u8 dss_getmem(void);
extern void dss_freemem(u8 block);
extern void dss_setwin(u8 win, u8 block);

/* copy slot text (in the currently-mapped WIN3 page) into tmp[] (WIN2) */
static void slot_to_tmp(u16 slot) {
    const char *src = (const char *)(WIN3 + (u16)slot * SLOT);
    u8 i = 0;
    while (i < SLOT - 1 && src[i]) { tmp[i] = src[i]; i++; }
    tmp[i] = 0;
}

/* write a (already-wrapped, <=WRAP) chunk into window idx's next ring slot */
static void store_chunk(u8 idx, const char *s) {
    Hist *h = &H[idx];
    u16 slot;
    char *dst;
    u8 i;
    if (h->page == NO_PAGE) return;
    if (h->lines < NSLOTS) { slot = (u16)((h->first + h->lines) % NSLOTS); h->lines++; }
    else { slot = h->first; h->first = (u16)((h->first + 1) % NSLOTS); }
    dss_setwin(3, h->page);                 /* map page -> WIN3; no DSS calls until after the copy */
    dst = (char *)(WIN3 + slot * SLOT);
    for (i = 0; i < SLOT - 1 && s[i]; i++) dst[i] = s[i];
    dst[i] = 0;
}

/* ---- public -------------------------------------------------------- */

void hist_init(void) {
    u8 i;
    for (i = 0; i < MAX_WIN; i++) { H[i].page = NO_PAGE; H[i].lines = 0; H[i].first = 0; H[i].view = -1; }
}

void hist_open(u8 idx) {
    H[idx].page = dss_getmem();             /* one 16K page; NO_PAGE (0xFF) if it fails */
    H[idx].lines = 0;
    H[idx].first = 0;
    H[idx].view = -1;
    H[idx].shown = 0;
}

void hist_close(u8 idx) {
    if (H[idx].page != NO_PAGE) { dss_freemem(H[idx].page); H[idx].page = NO_PAGE; }
    H[idx].lines = 0; H[idx].first = 0; H[idx].view = -1;
}

u8 hist_is_live(u8 idx) { return H[idx].view < 0; }

/* draw CHAT_H rows from the page for the current view (view<0 = live/bottom) */
static void render(u8 idx) {
    Hist *h = &H[idx];
    u16 top, li;
    u8 r, drawn = 0;
    if (h->page == NO_PAGE) { term_clear_chat(); h->shown = 0; return; }
    top = (h->view < 0) ? ((h->lines > CHAT_H) ? (u16)(h->lines - CHAT_H) : 0) : (u16)h->view;
    for (r = 0; r < CHAT_H; r++) {
        li = top + r;
        if (li < h->lines) {
            dss_setwin(3, h->page);                     /* map page -> WIN3 */
            slot_to_tmp((u16)((h->first + li) % NSLOTS)); /* copy to WIN2 before any DSS call */
            term_draw_row(r, tmp);
            drawn = (u8)(r + 1);
        } else {
            term_draw_row(r, "");
        }
    }
    h->shown = (h->view < 0) ? drawn : CHAT_H;
}

/* draw one new live line incrementally (scroll up if the area is full) */
static void live_append(u8 idx, const char *s) {
    Hist *h = &H[idx];
    if (h->view >= 0) return;                /* scrolled back: stored only, don't disturb */
    if (h->shown < CHAT_H) { term_draw_row(h->shown, s); h->shown++; }
    else { term_scroll_chat(); term_draw_row(CHAT_H - 1, s); }
}

/* word-wrap line into <=WRAP chunks, store each; if show, append each live */
void hist_add(u8 idx, const char *line, u8 show) {
    char chunk[WRAP + 1];
    const char *s = line;
    u8 i, brk;
    for (;;) {
        i = 0;
        while (s[i] && i < WRAP) i++;
        if (s[i] == 0) { store_chunk(idx, s); if (show) live_append(idx, s); break; }
        brk = WRAP;
        while (brk > 0 && s[brk] != ' ') brk--;
        if (brk == 0) brk = WRAP;
        for (i = 0; i < brk; i++) chunk[i] = s[i];
        chunk[i] = 0;
        store_chunk(idx, chunk);
        if (show) live_append(idx, chunk);
        s += brk;
        while (*s == ' ') s++;
        if (*s == 0) break;
    }
}

void hist_feed_tail(u8 idx) { H[idx].view = -1; render(idx); }

void hist_scroll(u8 idx, i8 dir) {
    Hist *h = &H[idx];
    i16 v;
    if (h->page == NO_PAGE || h->lines <= CHAT_H) return;   /* nothing to scroll */
    v = (h->view < 0) ? (i16)(h->lines - CHAT_H) : h->view;
    if (dir < 0) {                                   /* PgUp */
        v -= (CHAT_H - 1);
        if (v < 0) v = 0;
        h->view = v;
    } else {                                         /* PgDn */
        v += (CHAT_H - 1);
        h->view = (v >= (i16)(h->lines - CHAT_H)) ? -1 : v;   /* back to live at bottom */
    }
    render(idx);
}
