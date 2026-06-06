/*
 * cfg.c — load/save SPTALK.CFG. See cfg.h.
 */
#include "cfg.h"

#define CFG_PATH "SPTALK.CFG"

static u8 up(u8 c) { return (c >= 'a' && c <= 'z') ? (u8)(c - 32) : c; }
static u8 key_eq(const char *k, const char *lit) {
    while (*lit) { if (up((u8)*k) != (u8)*lit) return 0; k++; lit++; }
    return *k == 0;
}
static u8 str_eq_ci(const char *a, const char *b) {
    while (*a && *b) { if (up((u8)*a) != up((u8)*b)) return 0; a++; b++; }
    return *a == *b;
}
static void s_cpy(char *d, const char *s, u8 max) {
    u8 i = 0; while (s[i] && i < (u8)(max - 1)) { d[i] = s[i]; i++; } d[i] = 0;
}
static void s_zero(char *d, u16 n) { while (n--) *d++ = 0; }

/* One scratch buffer shared by load and save (never used concurrently). */
static char cfg_buf[512];

/* "SRV1".."SRV5" -> 0..4, else 0xFF */
static u8 srv_key_idx(const char *k) {
    if (up((u8)k[0]) == 'S' && up((u8)k[1]) == 'R' && up((u8)k[2]) == 'V'
        && k[3] >= '1' && k[3] <= ('0' + CFG_MAX_SRV) && k[4] == 0)
        return (u8)(k[3] - '1');
    return 0xFF;
}

void cfg_add_server(settings_t *s, const char *host, const char *port) {
    char e[CFG_SRV_LEN];
    u8 i, j, n = 0;
    char *o = e;
    const char *p = host;
    if (!host || !*host) return;
    while (*p && n < (u8)(CFG_SRV_LEN - 1)) { *o++ = *p++; n++; }
    if (port && port[0] && n < (u8)(CFG_SRV_LEN - 2)) {
        *o++ = ' '; n++; p = port;
        while (*p && n < (u8)(CFG_SRV_LEN - 1)) { *o++ = *p++; n++; }
    }
    *o = 0;

    for (i = 0; i < s->nsrv; i++)                 /* drop an existing duplicate */
        if (str_eq_ci(s->srv[i], e)) {
            for (j = i; j + 1 < s->nsrv; j++) s_cpy(s->srv[j], s->srv[j + 1], CFG_SRV_LEN);
            s->nsrv--;
            break;
        }
    if (s->nsrv >= CFG_MAX_SRV) s->nsrv = CFG_MAX_SRV - 1;   /* make room (drop oldest) */
    for (i = s->nsrv; i > 0; i--) s_cpy(s->srv[i], s->srv[i - 1], CFG_SRV_LEN);
    s_cpy(s->srv[0], e, CFG_SRV_LEN);
    s->nsrv++;
}

i8 cfg_load(settings_t *c) {
    char legacy_host[40], legacy_port[8];
    i16 fd, n;
    char *s, *start, *eq;
    s_zero((char *)c, sizeof(*c));
    legacy_host[0] = 0; legacy_port[0] = 0;

    fd = dss_open(CFG_PATH, O_RDONLY);
    if (fd < 0) return -1;
    n = dss_read((u8)fd, cfg_buf, sizeof(cfg_buf) - 1);
    dss_close((u8)fd);
    if (n <= 0) return -1;
    cfg_buf[n] = 0;

    s = cfg_buf;
    while (*s) {
        start = s;
        while (*s && *s != '\r' && *s != '\n') s++;
        if (*s) { *s = 0; s++; }
        while (*start == ' ') start++;
        if (*start && *start != '#') {
            eq = start; while (*eq && *eq != '=') eq++;
            if (*eq == '=') {
                u8 si;
                *eq = 0;
                si = srv_key_idx(start);
                if (key_eq(start, "NICK")) s_cpy(c->nick, eq + 1, sizeof(c->nick));
                else if (key_eq(start, "NICKPASS")) s_cpy(c->nspass, eq + 1, sizeof(c->nspass));
                else if (key_eq(start, "SERVER")) s_cpy(legacy_host, eq + 1, sizeof(legacy_host));
                else if (key_eq(start, "PORT")) s_cpy(legacy_port, eq + 1, sizeof(legacy_port));
                else if (si != 0xFF) {
                    s_cpy(c->srv[si], eq + 1, CFG_SRV_LEN);
                    if ((u8)(si + 1) > c->nsrv) c->nsrv = (u8)(si + 1);
                }
            }
        }
        while (*s == '\r' || *s == '\n') s++;
    }
    /* migrate an old SERVER=/PORT= config into the recent list */
    if (!c->nsrv && legacy_host[0]) cfg_add_server(c, legacy_host, legacy_port);
    c->loaded = 1;
    return 0;
}

static void put(char *buf, u16 *pos, const char *key, const char *val) {
    const char *p;
    if (!val[0]) return;
    for (p = key; *p; p++) buf[(*pos)++] = *p;
    buf[(*pos)++] = '=';
    for (p = val; *p; p++) buf[(*pos)++] = *p;
    buf[(*pos)++] = '\r'; buf[(*pos)++] = '\n';
}

i8 cfg_save(const settings_t *c) {
    char key[6];
    u16 pos = 0;
    i16 fd, w;
    u8 i;
    put(cfg_buf, &pos, "NICK", c->nick);
    put(cfg_buf, &pos, "NICKPASS", c->nspass);
    for (i = 0; i < c->nsrv && i < CFG_MAX_SRV; i++) {
        key[0] = 'S'; key[1] = 'R'; key[2] = 'V'; key[3] = (char)('1' + i); key[4] = 0;
        put(cfg_buf, &pos, key, c->srv[i]);
    }
    fd = dss_creat(CFG_PATH);
    if (fd < 0) return -1;
    w = dss_write((u8)fd, cfg_buf, pos);
    dss_close((u8)fd);
    return (w == (i16)pos) ? 0 : -1;
}
