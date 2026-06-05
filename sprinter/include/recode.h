/*
 * recode.h — UTF-8 <-> CP866 text recoding for public IRC channels.
 *
 * Cyrillic maps by formula (no big table): CP866 0x80-0x9F=А-Я, 0xA0-0xAF=а-п,
 * 0xE0-0xEF=р-я, 0xF0/0xF1=Ё/ё <-> Unicode U+0410.., plus a few common
 * punctuation. Receive shortens (multibyte->1), send expands (1->2 bytes).
 */
#ifndef RECODE_H
#define RECODE_H

#include <sprinter.h>

void utf8_to_cp866(char *s);                            /* in place (output <= input) */
void cp866_to_utf8(const char *src, char *dst, u16 max);/* dst NUL-terminated, bounded */

#endif /* RECODE_H */
