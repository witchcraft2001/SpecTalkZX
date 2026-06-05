/*
 * cfg.h — persist last-used settings (server/port/nick) in SPECTALK.CFG.
 *
 * Saved in the current directory as a small INI (SERVER=/PORT=/NICK=). On
 * startup the client loads it to set the nick and offer the last server via the
 * input history; saved on successful /server and /nick.
 */
#ifndef CFG_H
#define CFG_H

#include <sprinter.h>

typedef struct {
    char server[40];
    char port[8];
    char nick[20];
    char nspass[32];   /* NickServ password (stored plaintext; see cfg.c) */
    u8   loaded;       /* 1 if SPECTALK.CFG was read */
} settings_t;

i8 cfg_load(settings_t *s);          /* 0 if loaded, <0 if absent */
i8 cfg_save(const settings_t *s);    /* 0 if written, <0 on error */

#endif /* CFG_H */
