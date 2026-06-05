/*
 * cfg.c — load/save SPECTALK.CFG. See cfg.h.
 */
#include "cfg.h"

#define CFG_PATH "SPECTALK.CFG"

static u8 up(u8 c) { return (c >= 'a' && c <= 'z') ? (u8)(c - 32) : c; }
static u8 key_eq(const char *k, const char *lit) {
    while (*lit) { if (up((u8)*k) != (u8)*lit) return 0; k++; lit++; }
    return *k == 0;
}
static void s_cpy(char *d, const char *s, u8 max) {
    u8 i = 0; while (s[i] && i < (u8)(max - 1)) { d[i] = s[i]; i++; } d[i] = 0;
}
static void s_zero(char *d, u16 n) { while (n--) *d++ = 0; }

i8 cfg_load(settings_t *c) {
    static char buf[256];
    i16 fd, n;
    char *s, *start, *eq;
    s_zero((char *)c, sizeof(*c));

    fd = dss_open(CFG_PATH, O_RDONLY);
    if (fd < 0) return -1;
    n = dss_read((u8)fd, buf, sizeof(buf) - 1);
    dss_close((u8)fd);
    if (n <= 0) return -1;
    buf[n] = 0;

    s = buf;
    while (*s) {
        start = s;
        while (*s && *s != '\r' && *s != '\n') s++;
        if (*s) { *s = 0; s++; }
        while (*start == ' ') start++;
        if (*start && *start != '#') {
            eq = start; while (*eq && *eq != '=') eq++;
            if (*eq == '=') {
                *eq = 0;
                if (key_eq(start, "SERVER")) s_cpy(c->server, eq + 1, sizeof(c->server));
                else if (key_eq(start, "PORT")) s_cpy(c->port, eq + 1, sizeof(c->port));
                else if (key_eq(start, "NICK")) s_cpy(c->nick, eq + 1, sizeof(c->nick));
            }
        }
        while (*s == '\r' || *s == '\n') s++;
    }
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
    static char buf[256];
    u16 pos = 0;
    i16 fd, w;
    put(buf, &pos, "SERVER", c->server);
    put(buf, &pos, "PORT", c->port);
    put(buf, &pos, "NICK", c->nick);
    fd = dss_creat(CFG_PATH);
    if (fd < 0) return -1;
    w = dss_write((u8)fd, buf, pos);
    dss_close((u8)fd);
    return (w == (i16)pos) ? 0 : -1;
}
