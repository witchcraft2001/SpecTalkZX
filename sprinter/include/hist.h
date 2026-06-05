/*
 * hist.h — per-window scrollback in DSS-allocated 16K pages.
 *
 * Each window gets its own DSS page (Dss.GetMem), holding a ring of fixed 80-byte
 * line slots (~204 lines). The page is mapped into WIN3 only for the brief memory
 * copy; lines are copied into a WIN2 buffer BEFORE any DSS call (which would
 * otherwise clobber WIN3). The live current window keeps using the fast term ring;
 * pages restore the tail on switch and back the PgUp/PgDn scrollback.
 */
#ifndef HIST_H
#define HIST_H

#include <sprinter.h>

void hist_init(void);                   /* mark all windows as having no page (call once) */
void hist_open(u8 idx);                 /* allocate a page for window idx (no-op if it fails) */
void hist_close(u8 idx);                /* free window idx's page */
void hist_add(u8 idx, const char *line, u8 show);  /* wrap+store; show -> also draw live */
void hist_feed_tail(u8 idx);            /* clear chat + redraw the last screenful (go live) */
void hist_scroll(u8 idx, i8 dir);       /* PgUp (dir<0) / PgDn (dir>0) through history */
u8   hist_is_live(u8 idx);              /* 1 if viewing the bottom (live) */

#endif /* HIST_H */
