/*
 * recode.c — UTF-8 <-> CP866 recoding. See recode.h.
 */
#include "recode.h"

/* Unicode codepoint -> CP866 byte (Cyrillic + common punctuation), else '?' */
static u8 uni_to_cp866(u16 cp) {
    if (cp >= 0x0410 && cp <= 0x042F) return (u8)(0x80 + (cp - 0x0410));  /* А-Я */
    if (cp >= 0x0430 && cp <= 0x043F) return (u8)(0xA0 + (cp - 0x0430));  /* а-п */
    if (cp >= 0x0440 && cp <= 0x044F) return (u8)(0xE0 + (cp - 0x0440));  /* р-я */
    if (cp == 0x0401) return 0xF0;                                        /* Ё */
    if (cp == 0x0451) return 0xF1;                                        /* ё */
    if (cp == 0x00A0) return ' ';                                         /* NBSP */
    if (cp == 0x2013 || cp == 0x2014) return '-';                         /* en/em dash */
    if (cp == 0x2018 || cp == 0x2019) return '\'';                        /* curly ' */
    if (cp == 0x201C || cp == 0x201D) return '"';                         /* curly " */
    if (cp == 0x2026) return '.';                                         /* ellipsis */
    if (cp < 0x80) return (u8)cp;
    return '?';
}

/* CP866 byte -> Unicode codepoint (Cyrillic); others pass as Latin-1 */
static u16 cp866_to_uni(u8 c) {
    if (c >= 0x80 && c <= 0x9F) return (u16)(0x0410 + (c - 0x80));  /* А-Я */
    if (c >= 0xA0 && c <= 0xAF) return (u16)(0x0430 + (c - 0xA0));  /* а-п */
    if (c >= 0xE0 && c <= 0xEF) return (u16)(0x0440 + (c - 0xE0));  /* р-я */
    if (c == 0xF0) return 0x0401;                                  /* Ё */
    if (c == 0xF1) return 0x0451;                                  /* ё */
    return c;
}

void utf8_to_cp866(char *s) {
    u8 *r = (u8 *)s, *w = (u8 *)s, c, b2, b3;
    u16 cp;
    while ((c = *r++) != 0) {
        if (c < 0x80) { *w++ = c; continue; }
        if ((c & 0xE0) == 0xC0) {                       /* 2-byte */
            b2 = *r;
            if ((b2 & 0xC0) != 0x80) { *w++ = '?'; continue; }
            r++;
            cp = (u16)(((u16)(c & 0x1F) << 6) | (b2 & 0x3F));
            *w++ = uni_to_cp866(cp);
        } else if ((c & 0xF0) == 0xE0) {                /* 3-byte */
            b2 = *r; if ((b2 & 0xC0) == 0x80) r++;
            b3 = *r; if ((b3 & 0xC0) == 0x80) r++;
            cp = (u16)(((u16)(c & 0x0F) << 12) | ((u16)(b2 & 0x3F) << 6) | (b3 & 0x3F));
            *w++ = uni_to_cp866(cp);
        } else if ((c & 0xF8) == 0xF0) {                /* 4-byte: out of BMP */
            if ((*r & 0xC0) == 0x80) r++;
            if ((*r & 0xC0) == 0x80) r++;
            if ((*r & 0xC0) == 0x80) r++;
            *w++ = '?';
        } else {
            *w++ = '?';                                  /* stray continuation/invalid */
        }
    }
    *w = 0;
}

void cp866_to_utf8(const char *src, char *dst, u16 max) {
    const u8 *s = (const u8 *)src;
    u8 *d = (u8 *)dst, *end = (u8 *)dst + (max - 1);
    u8 c;
    u16 cp;
    while ((c = *s++) != 0 && d < end) {
        if (c < 0x80) { *d++ = c; continue; }
        cp = cp866_to_uni(c);
        if (cp < 0x80) { *d++ = (u8)cp; }
        else if (d + 1 < end) { *d++ = (u8)(0xC0 | (cp >> 6)); *d++ = (u8)(0x80 | (cp & 0x3F)); }
    }
    *d = 0;
}
