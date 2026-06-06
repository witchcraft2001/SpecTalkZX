/*
 * cfg.h — persist last-used settings in SPTALK.CFG.
 *
 * Saved in the current directory as a small INI. Keys: NICK, NICKPASS, and
 * SRV1..SRVn (recent servers as "host[ port]", newest first). On startup the
 * client sets the nick, loads the NickServ password, and seeds the input recall
 * with the recent servers. Saved on /server and /nick.
 */
#ifndef CFG_H
#define CFG_H

#include <sprinter.h>

#define CFG_MAX_SRV 5          /* how many recent servers we remember */
#define CFG_SRV_LEN 52         /* "host port" entry length */

typedef struct {
    char nick[20];
    char nspass[32];                  /* NickServ password (stored plaintext) */
    char srv[CFG_MAX_SRV][CFG_SRV_LEN]; /* recent "host[ port]", [0] = newest */
    u8   nsrv;                        /* number of valid srv[] entries */
    u8   loaded;                      /* 1 if SPTALK.CFG was read */
} settings_t;

i8 cfg_load(settings_t *s);          /* 0 if loaded, <0 if absent */
i8 cfg_save(const settings_t *s);    /* 0 if written, <0 on error */

/* Push a server to the front of the recent list (dedup, cap at CFG_MAX_SRV). */
void cfg_add_server(settings_t *s, const char *host, const char *port);

#endif /* CFG_H */
