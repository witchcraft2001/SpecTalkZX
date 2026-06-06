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

/* "<PREFIX><n>" (n = 1..maxn) -> 0..maxn-1, else 0xFF */
static u8 numbered_key(const char *k, const char *prefix, u8 maxn) {
    u8 i = 0;
    while (prefix[i]) { if (up((u8)k[i]) != (u8)prefix[i]) return 0xFF; i++; }
    if (k[i] >= '1' && k[i] <= (char)('0' + maxn) && k[i + 1] == 0) return (u8)(k[i] - '1');
    return 0xFF;
}

/* shared front-insert with dedup into a fixed-stride string list */
static void list_push(char *list, u8 stride, u8 max, u8 *count, const char *e) {
    u8 i, j;
    for (i = 0; i < *count; i++)
        if (str_eq_ci(&list[i * stride], e)) {
            for (j = i; j + 1 < *count; j++) s_cpy(&list[j * stride], &list[(j + 1) * stride], stride);
            (*count)--;
            break;
        }
    if (*count >= max) *count = (u8)(max - 1);
    for (i = *count; i > 0; i--) s_cpy(&list[i * stride], &list[(i - 1) * stride], stride);
    s_cpy(&list[0], e, stride);
    (*count)++;
}

void cfg_add_server(settings_t *s, const char *host, const char *port) {
    char e[CFG_SRV_LEN];
    u8 n = 0;
    char *o = e;
    const char *p = host;
    if (!host || !*host) return;
    while (*p && n < (u8)(CFG_SRV_LEN - 1)) { *o++ = *p++; n++; }
    if (port && port[0] && n < (u8)(CFG_SRV_LEN - 2)) {
        *o++ = ' '; n++; p = port;
        while (*p && n < (u8)(CFG_SRV_LEN - 1)) { *o++ = *p++; n++; }
    }
    *o = 0;
    list_push(&s->srv[0][0], CFG_SRV_LEN, CFG_MAX_SRV, &s->nsrv, e);
}

void cfg_add_chan(settings_t *s, const char *chan) {
    char e[CFG_CHAN_LEN];
    u8 n = 0;
    char *o = e;
    const char *p = chan;
    if (!chan || !*chan) return;
    while (*p && *p != ' ' && n < (u8)(CFG_CHAN_LEN - 1)) { *o++ = *p++; n++; }  /* first token only */
    *o = 0;
    if (!e[0]) return;
    list_push(&s->chan[0][0], CFG_CHAN_LEN, CFG_MAX_CHAN, &s->nchan, e);
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
                u8 si, ci;
                *eq = 0;
                si = numbered_key(start, "SRV", CFG_MAX_SRV);
                ci = numbered_key(start, "CHAN", CFG_MAX_CHAN);
                if (key_eq(start, "NICK")) s_cpy(c->nick, eq + 1, sizeof(c->nick));
                else if (key_eq(start, "NICKPASS")) s_cpy(c->nspass, eq + 1, sizeof(c->nspass));
                else if (key_eq(start, "SERVER")) s_cpy(legacy_host, eq + 1, sizeof(legacy_host));
                else if (key_eq(start, "PORT")) s_cpy(legacy_port, eq + 1, sizeof(legacy_port));
                else if (si != 0xFF) {
                    s_cpy(c->srv[si], eq + 1, CFG_SRV_LEN);
                    if ((u8)(si + 1) > c->nsrv) c->nsrv = (u8)(si + 1);
                } else if (ci != 0xFF) {
                    s_cpy(c->chan[ci], eq + 1, CFG_CHAN_LEN);
                    if ((u8)(ci + 1) > c->nchan) c->nchan = (u8)(ci + 1);
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
    char key[7];
    u16 pos = 0;
    i16 fd, w;
    u8 i;
    put(cfg_buf, &pos, "NICK", c->nick);
    put(cfg_buf, &pos, "NICKPASS", c->nspass);
    for (i = 0; i < c->nsrv && i < CFG_MAX_SRV; i++) {
        key[0] = 'S'; key[1] = 'R'; key[2] = 'V'; key[3] = (char)('1' + i); key[4] = 0;
        put(cfg_buf, &pos, key, c->srv[i]);
    }
    for (i = 0; i < c->nchan && i < CFG_MAX_CHAN; i++) {
        key[0] = 'C'; key[1] = 'H'; key[2] = 'A'; key[3] = 'N'; key[4] = (char)('1' + i); key[5] = 0;
        put(cfg_buf, &pos, key, c->chan[i]);
    }
    fd = dss_creat(CFG_PATH);
    if (fd < 0) return -1;
    w = dss_write((u8)fd, cfg_buf, pos);
    dss_close((u8)fd);
    return (w == (i16)pos) ? 0 : -1;
}
