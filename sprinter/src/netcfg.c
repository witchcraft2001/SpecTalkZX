/*
 * netcfg.c — load/parse NET.CFG. See netcfg.h.
 * Self-contained string helpers (no libc dependency).
 */
#include "netcfg.h"

/* ---- tiny string helpers ------------------------------------------- */

static u8 s_len(const char *s) { u8 n = 0; while (s[n]) n++; return n; }

static void s_cpy(char *d, const char *s, u8 max) {   /* max incl. NUL */
    u8 i = 0;
    while (s[i] && i < (u8)(max - 1)) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static void s_zero(char *d, u16 n) { while (n--) *d++ = 0; }

static u8 up(u8 c) { return (c >= 'a' && c <= 'z') ? (u8)(c - 32) : c; }

/* case-insensitive compare of a NUL-terminated key against literal lit */
static u8 key_eq(const char *k, const char *lit) {
    while (*lit) { if (up((u8)*k) != (u8)*lit) return 0; k++; lit++; }
    return *k == 0;
}

/* ---- parsing -------------------------------------------------------- */

static void set_field(netcfg_t *c, const char *key, const char *val) {
    if      (key_eq(key, "SSID"))    s_cpy(c->ssid, val, sizeof(c->ssid));
    else if (key_eq(key, "PASS"))    s_cpy(c->pass, val, sizeof(c->pass));
    else if (key_eq(key, "DHCP"))    s_cpy(c->dhcp, val, sizeof(c->dhcp));
    else if (key_eq(key, "IP"))      s_cpy(c->ip, val, sizeof(c->ip));
    else if (key_eq(key, "GATEWAY")) s_cpy(c->gateway, val, sizeof(c->gateway));
    else if (key_eq(key, "NETMASK")) s_cpy(c->netmask, val, sizeof(c->netmask));
    else if (key_eq(key, "DNS1"))    s_cpy(c->dns1, val, sizeof(c->dns1));
    else if (key_eq(key, "DNS2"))    s_cpy(c->dns2, val, sizeof(c->dns2));
    else if (key_eq(key, "NTP"))     s_cpy(c->ntp, val, sizeof(c->ntp));
    else if (key_eq(key, "BAUD"))    s_cpy(c->baud, val, sizeof(c->baud));
}

/* parse one line "KEY=VALUE" (already NUL-terminated, no CR/LF) */
static void parse_line(netcfg_t *c, char *line) {
    char *p = line, *eq;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == 0 || *p == '#' || *p == ';') return;
    eq = p;
    while (*eq && *eq != '=') eq++;
    if (*eq != '=') return;
    *eq = 0;                       /* terminate key */
    set_field(c, p, eq + 1);
}

i8 netcfg_load(netcfg_t *c) {
    static char buf[640];
    char path[80];
    i16 fd, n;
    char *s, *start;
    u8 L;

    s_zero((char *)c, sizeof(*c));

    /* %NET_DIR% */
    if (dss_getenv("NET_DIR", c->net_dir) != 0) c->net_dir[0] = 0;

    /* build <NET_DIR>\NET.CFG (or NET.CFG in CWD) */
    if (c->net_dir[0]) {
        s_cpy(path, c->net_dir, sizeof(path));
        L = s_len(path);
        if (L && path[L - 1] != '\\' && path[L - 1] != '/') { path[L++] = '\\'; path[L] = 0; }
        s_cpy(path + L, "NET.CFG", (u8)(sizeof(path) - L));
    } else {
        s_cpy(path, "NET.CFG", sizeof(path));
    }
    s_cpy(c->path, path, sizeof(c->path));

    fd = dss_open(path, O_RDONLY);
    if (fd < 0 && c->net_dir[0]) {        /* fall back to current dir */
        s_cpy(c->path, "NET.CFG", sizeof(c->path));
        fd = dss_open("NET.CFG", O_RDONLY);
    }
    if (fd < 0) return -1;

    n = dss_read((u8)fd, buf, sizeof(buf) - 1);
    dss_close((u8)fd);
    if (n <= 0) return -2;
    buf[n] = 0;

    /* split on CR/LF and parse each line */
    s = buf;
    while (*s) {
        start = s;
        while (*s && *s != '\r' && *s != '\n') s++;
        if (*s) { *s = 0; s++; }
        parse_line(c, start);
        while (*s == '\r' || *s == '\n') s++;
    }

    c->loaded = 1;
    return 0;
}
