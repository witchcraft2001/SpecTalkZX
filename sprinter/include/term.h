/*
 * term.h — terminal HAL for the SpecTalk Sprinter port.
 *
 * Owns the 80x32 screen layout (banner / chat scrollback / status / notify /
 * input) and the primitives the IRC/UI layers draw through. Built on the DSS
 * windowed text primitives (dss_clear / dss_scroll / dss_gotoxy / dss_putchar).
 *
 * Coordinates are DSS 1-based. Attribute bytes are %PPPPIIII (paper high nibble,
 * ink low nibble); text printed into a region inherits the region's attribute.
 */
#ifndef TERM_H
#define TERM_H

#include <sprinter.h>

#define SCR_W       80
#define SCR_H       32

/* Row map (1-based) */
#define BANNER_ROW  1
#define CHAT_TOP    3
#define CHAT_H      26              /* rows 3..28 */
#define CHAT_BOT    (CHAT_TOP + CHAT_H - 1)
#define SEP_ROW     29
#define STATUS_ROW  30
#define NOTIF_ROW   31
#define INPUT_ROW   32

/* Attributes (paper<<4 | ink); palette is ZX-like: 0=blk 1=blu 2=red 3=mag
 * 4=grn 5=cyn 6=yel 7=wht */
#define ATTR_NORMAL 0x0F            /* white on black */
#define ATTR_BANNER 0x17            /* white on blue  */
#define ATTR_STATUS 0x70            /* black on white (inverse bar) */
#define ATTR_NOTIF  0x06            /* yellow on black */
#define ATTR_SEP    0x08            /* dim blue ink on black */

void term_init(void);
void term_banner(const char *title);   /* left side of banner row */
void term_clock(void);                  /* HH:MM:SS at right of banner row */
void term_add_line(const char *s);      /* append to chat area, scroll if full */
void term_status(const char *s);        /* status bar text */
void term_notif(const char *s);         /* notification line text */
void term_input(const char *buf, u8 len, u8 cur);  /* redraw input row + cursor */

#endif /* TERM_H */
