/*
 * irc.c — IRC engine (window model + parser/dispatch + handlers). See irc.h.
 * Adapted from the original SpecTalk irc_handlers.c, de-ZX'd to clean C on the
 * Sprinter HAL (term_*) and transport (net_*).
 */
#include "irc.h"
#include "version.h"
#include "term.h"
#include "net.h"
#include "hist.h"
#include "recode.h"

#define WIN_NAME  24
#define F_ACTIVE  0x01
#define F_QUERY   0x02
#define F_UNREAD  0x04
#define F_MENTION 0x08
#define F_SERVER  0x10

typedef struct {
    char name[WIN_NAME];
    u8   flags;
    u16  users;
    u8   page;          /* reserved: DSS history page block id (future) */
} Window;

#define MAX_IGN 5

static Window win[MAX_WIN];
static u8     wcur;
static char   mynick[20];
static u8     registered;
static u8     ts_on = 1;                 /* timestamp messages */
static u8     enc_on = 1;                /* UTF-8 <-> CP866 recoding (public channels) */
static char   ign[MAX_IGN][20];          /* ignored nicks */
static char   tsbuf[488];                /* "[HH:MM] " + line */
static char   sendbuf[816];              /* CP866->UTF-8 expansion for outgoing text */
static char   nspass[32];                /* NickServ password (persisted in cfg) */
static u8     ns_done;                   /* auto-identify fired this connection */
static u16    idle_s;                    /* seconds since last RX (keepalive timer) */
static u8     ka_pinged;                 /* keepalive PING already sent this idle period */
static u8     link_up;                   /* link considered established (one-shot teardown) */

#define KA_PING_S    150                 /* idle this long -> send a keepalive PING */
#define KA_TIMEOUT_S 300                 /* idle this long with no reply -> link is dead */

/* ---- string helpers ------------------------------------------------ */
static u8 lc(u8 c) { return (c >= 'A' && c <= 'Z') ? (u8)(c + 32) : c; }
static u8 seq(const char *a, const char *b) { while (*a && *b) { if (*a != *b) return 0; a++; b++; } return *a == *b; }
static u8 ieq(const char *a, const char *b) { while (*a && *b) { if (lc((u8)*a) != lc((u8)*b)) return 0; a++; b++; } return *a == *b; }
static u8 istr(const char *h, const char *n) {            /* case-insensitive contains */
    const char *a, *b;
    for (; *h; h++) { a = h; b = n; while (*a && *b && lc((u8)*a) == lc((u8)*b)) { a++; b++; } if (!*b) return 1; }
    return 0;
}
static void s_cpy(char *d, const char *s, u8 max) { u8 i = 0; while (s[i] && i < (u8)(max - 1)) { d[i] = s[i]; i++; } d[i] = 0; }
static u16 to_u16(const char *s) { u16 v = 0; while (*s >= '0' && *s <= '9') { v = (u16)(v * 10 + (*s - '0')); s++; } return v; }

/* ---- output line builder ------------------------------------------ */
static char  out[480];   /* must hold "<nick> " + a full ~400-char message */
static char *ob;
static void o_init(void) { ob = out; }
static void o_str(const char *s) { while (*s && ob < out + sizeof(out) - 1) *ob++ = *s++; }
static void o_c(char c) { if (ob < out + sizeof(out) - 1) *ob++ = c; }
static void o_end(void) { *ob = 0; }

/* ---- windows ------------------------------------------------------- */
static void banner_refresh(void) {
    o_init(); o_str(APP_TITLE); o_str("  ::  "); o_str(win[wcur].name); o_end();
    term_banner(out);
}

static u8 net_warn;                  /* show a link-trouble marker in the status bar */

static void status_refresh(void) {
    u8 i;
    o_init();
    o_str("nick:"); o_str(mynick); o_str("  [");
    for (i = 0; i < MAX_WIN; i++) {
        if (!(win[i].flags & F_ACTIVE)) continue;
        if (i == wcur) o_c('>');
        o_c((char)('0' + i));
        if (win[i].flags & F_MENTION) o_c('!');
        else if (win[i].flags & F_UNREAD) o_c('*');
        o_c(' ');
    }
    o_c(']');
    if (net_warn) o_str("  *** ESP NOT RESPONDING ***");
    else if (registered) o_str("  online");
    o_end();
    term_status(out);
}

void irc_net_warn(u8 on) { if (net_warn != on) { net_warn = on; status_refresh(); } }

static i8 win_find(const char *name) {
    u8 i;
    for (i = 0; i < MAX_WIN; i++)
        if ((win[i].flags & F_ACTIVE) && ieq(win[i].name, name)) return (i8)i;
    return -1;
}

static i8 win_add(const char *name, u8 flags) {
    u8 i;
    for (i = 1; i < MAX_WIN; i++) {            /* slot 0 reserved for server */
        if (!(win[i].flags & F_ACTIVE)) {
            s_cpy(win[i].name, name, WIN_NAME);
            win[i].flags = (u8)(F_ACTIVE | flags);
            win[i].users = 0;
            hist_open(i);
            return (i8)i;
        }
    }
    return -1;
}

static void win_switch(u8 idx) {
    if (idx >= MAX_WIN || !(win[idx].flags & F_ACTIVE)) return;
    wcur = idx;
    win[idx].flags &= (u8)~(F_UNREAD | F_MENTION);
    hist_feed_tail(idx);        /* restore this window's recent history */
    banner_refresh();
    status_refresh();
}

static void win_close(u8 idx) {
    if (idx == 0) return;       /* never close the server window */
    if (win[idx].flags & F_ACTIVE) {
        win[idx].flags = 0;
        win[idx].name[0] = 0;
        hist_close(idx);
        if (idx == wcur) win_switch(0);
        else status_refresh();
    }
}

static u8 is_ignored(const char *nick) {
    u8 i;
    for (i = 0; i < MAX_IGN; i++) if (ign[i][0] && ieq(ign[i], nick)) return 1;
    return 0;
}

/* route a finished line to a window: prepend timestamp, strip IRC formatting
 * control codes (bold/colour/underline/… render as junk box glyphs on the
 * Sprinter font — e.g. NickServ wraps names in bold 0x02), store in history. */
static void win_print(i8 idx, const char *line) {
    u8 w;
    char *o = tsbuf;
    const u8 *p = (const u8 *)line;
    const char *end = tsbuf + sizeof(tsbuf) - 1;
    if (idx < 0) idx = 0;
    w = (u8)idx;
    if (ts_on) {                         /* prepend "[HH:MM] " */
        dss_time_t t;
        dss_gettime(&t);
        *o++ = '['; *o++ = (char)('0' + (t.hour / 10) % 10); *o++ = (char)('0' + t.hour % 10);
        *o++ = ':'; *o++ = (char)('0' + (t.minute / 10) % 10); *o++ = (char)('0' + t.minute % 10);
        *o++ = ']'; *o++ = ' ';
    }
    while (*p && o < end) {
        u8 c = *p++;
        if (c == 0x03) {                 /* colour: optional N[,M] digits */
            if (*p >= '0' && *p <= '9') { p++; if (*p >= '0' && *p <= '9') p++;
                if (*p == ',' && p[1] >= '0' && p[1] <= '9') { p += 2; if (*p >= '0' && *p <= '9') p++; } }
            continue;
        }
        if (c == 0x04) {                 /* hex colour: up to 6 hex digits */
            u8 k = 0;
            while (k < 6 && ((*p >= '0' && *p <= '9') || ((*p | 32) >= 'a' && (*p | 32) <= 'f'))) { p++; k++; }
            continue;
        }
        if (c < 0x20) continue;          /* bold/italic/underline/reset/reverse/… -> drop */
        *o++ = (char)c;
    }
    *o = 0;
    hist_add(w, tsbuf, (u8)(w == wcur && hist_is_live(w)));
    if (w != wcur) { win[w].flags |= F_UNREAD; status_refresh(); }
}

static u8 has_dot(const char *s) { while (*s) { if (*s == '.') return 1; s++; } return 0; }

static i8 route_win(const char *usr, const char *target) {
    i8 w;
    if (target[0] == '#' || target[0] == '&') { w = win_find(target); return (w < 0) ? 0 : w; }
    w = win_find(usr);
    if (w < 0) w = win_add(usr, F_QUERY);
    return (w < 0) ? 0 : w;
}

/* ---- params -------------------------------------------------------- */
static char *argv[6];
static u8    argc;
static void split_params(char *par) {
    argc = 0;
    while (*par && argc < 6) {
        while (*par == ' ') par++;
        if (!*par) break;
        argv[argc++] = par;
        while (*par && *par != ' ') par++;
        if (*par) *par++ = 0;
    }
}
static const char *arg(u8 i) { return (i < argc) ? argv[i] : ""; }

/* ---- CTCP ---------------------------------------------------------- */
static void handle_ctcp(const char *usr, const char *target, char *c) {
    char *e = c;
    while (*e && *e != 1) e++;
    *e = 0;                                       /* strip trailing 0x01 */
    if (seq(c, "ACTION") && c[6] == ' ') {
        o_init(); o_str("* "); o_str(usr); o_c(' '); o_str(c + 7); o_end();
        win_print(route_win(usr, target), out);
    } else if (seq(c, "VERSION")) {
        net_send("NOTICE "); net_send(usr); net_send(" :\001VERSION " APP_TITLE " (Sprinter)\001\r\n");
    } else if (seq(c, "PING")) {
        net_send("NOTICE "); net_send(usr); net_send(" :\001PING"); net_send(c + 4); net_send("\001\r\n");
    }
}

/* ---- handlers ------------------------------------------------------ */
static void h_privmsg(const char *usr, char *target, char *txt, u8 is_notice) {
    i8 w;
    u8 mention;
    if (!*target || !*txt) return;
    if (is_ignored(usr)) return;
    if (txt[0] == 1) { handle_ctcp(usr, target, txt + 1); return; }

    /* Auto-IDENTIFY: a NickServ-like service asking us to identify. Only fire if
     * a password is stored, the sender looks like a *Serv (not a random user, so
     * we never leak the password), and once per connection. */
    if (nspass[0] && !ns_done && is_notice && target[0] != '#' && target[0] != '&'
        && istr(usr, "serv") && istr(txt, "identify")) {
        ns_done = 1;
        irc_identify(nspass);
        return;
    }

    /* server NOTICEs (sender is a server host, or target '*'/'AUTH') -> server window */
    if (is_notice && (target[0] == '*' || has_dot(usr)))
        w = 0;
    else
        w = route_win(usr, target);
    mention = (target[0] == '#' && mynick[0] && istr(txt, mynick));

    o_init();
    if (is_notice) { o_c('-'); o_str(usr); o_str("- "); }
    else           { o_c('<'); o_str(usr); o_str("> "); }
    o_str(txt);
    o_end();
    win_print(w, out);
    if (mention && (u8)w != wcur) { win[(u8)w].flags |= F_MENTION; status_refresh(); }
}

static void h_join(const char *usr, char *chan) {
    i8 w;
    if (*chan == ':') chan++;
    if (!*chan) return;
    if (ieq(usr, mynick)) {
        w = win_find(chan);
        if (w < 0) w = win_add(chan, 0);
        if (w < 0) { win_print(0, "* cannot open window (max reached)"); return; }
        win_switch((u8)w);
        o_init(); o_str("* now talking in "); o_str(chan); o_end();
        win_print(w, out);
    } else {
        w = win_find(chan);
        if (w >= 0) {
            win[(u8)w].users++;
            o_init(); o_str("--> "); o_str(usr); o_str(" joined"); o_end();
            win_print(w, out);
        }
    }
}

static void h_part(const char *usr, char *chan) {
    i8 w = win_find(chan);
    if (w < 0) return;
    if (ieq(usr, mynick)) { win_close((u8)w); return; }
    if (win[(u8)w].users) win[(u8)w].users--;
    o_init(); o_str("<-- "); o_str(usr); o_str(" left"); o_end();
    win_print(w, out);
}

static void h_quit(const char *usr, const char *txt) {
    o_init(); o_str("<-- "); o_str(usr); o_str(" quit");
    if (*txt) { o_str(" ("); o_str(txt); o_c(')'); }
    o_end();
    win_print((i8)wcur, out);     /* simple: current window (TODO: all shared) */
}

static void h_nick(const char *usr, const char *newn) {
    if (*newn == ':') newn++;
    o_init(); o_str("* "); o_str(usr); o_str(" is now "); o_str(newn); o_end();
    if (ieq(usr, mynick)) { s_cpy(mynick, newn, sizeof(mynick)); status_refresh(); }
    win_print((i8)wcur, out);
}

static void h_mode(char *target, const char *txt) {
    i8 w = (target[0] == '#') ? win_find(target) : (i8)wcur;
    o_init(); o_str("* mode "); o_str(target); o_c(' ');
    { u8 i; for (i = 1; i < argc; i++) { o_str(arg(i)); o_c(' '); } }
    if (*txt) o_str(txt);
    o_end();
    win_print(w, out);
}

static void h_kick(const char *usr, char *chan, const char *who, const char *txt) {
    i8 w = win_find(chan);
    o_init(); o_str("* "); o_str(who); o_str(" was kicked by "); o_str(usr);
    if (*txt) { o_str(" ("); o_str(txt); o_c(')'); }
    o_end();
    win_print(w, out);
}

static void h_topic(const char *usr, char *chan, const char *txt) {
    i8 w = win_find(chan);
    o_init(); o_str("* "); o_str(usr); o_str(" set topic: "); o_str(txt); o_end();
    win_print(w, out);
}

static void h_numeric(u16 n, const char *txt) {
    i8 w;
    if (n == 332) {                                   /* RPL_TOPIC: <me> #chan :topic */
        w = win_find(arg(1));
        o_init(); o_str("* topic: "); o_str(txt); o_end();
        win_print(w, out);
    } else if (n == 353) {                            /* RPL_NAMREPLY: <me> = #chan :nicks */
        w = win_find(arg(2));
        o_init(); o_str("* users: "); o_str(txt); o_end();
        win_print(w, out);
    } else if (n == 366 || n == 333) {
        /* end-of-names / topic-set-by: quietly ignore */
    } else if (n == 1) {
        registered = 1;
        win_print(0, txt);
        term_notif("connected. /join #channel to chat.  TAB=window  /help");
    } else if (n == 433) {                            /* nick in use -> append '_' */
        u8 l = 0; while (mynick[l]) l++;
        if (l < sizeof(mynick) - 1) { mynick[l] = '_'; mynick[l + 1] = 0; }
        net_send("NICK "); net_send(mynick); net_send("\r\n");
        o_init(); o_str("* nick in use, trying "); o_str(mynick); o_end();
        win_print(0, out);
        status_refresh();
    } else {
        /* generic numeric: show params (skip arg[0]=our nick) + trailing text */
        u8 k;
        o_init();
        for (k = 1; k < argc; k++) { o_str(arg(k)); o_c(' '); }
        o_str(txt);
        o_end();
        win_print(0, (out[0]) ? out : "(numeric)");
    }
}

/* ---- dispatch ------------------------------------------------------ */
static void dispatch(char *usr, char *cmd, char *par, char *txt) {
    split_params(par);
    if (seq(cmd, "PING")) {
        net_send("PONG :"); net_send(*txt ? txt : arg(0)); net_send("\r\n");
    } else if (seq(cmd, "PONG")) {
        /* reply to our keepalive PING — RX already reset the timer; don't show it */
    } else if (seq(cmd, "PRIVMSG")) {
        h_privmsg(usr, (char *)arg(0), txt, 0);
    } else if (seq(cmd, "NOTICE")) {
        h_privmsg(usr, (char *)arg(0), txt, 1);
    } else if (seq(cmd, "JOIN")) {
        h_join(usr, (char *)(argc ? arg(0) : txt));
    } else if (seq(cmd, "PART")) {
        h_part(usr, (char *)arg(0));
    } else if (seq(cmd, "QUIT")) {
        h_quit(usr, txt);
    } else if (seq(cmd, "NICK")) {
        h_nick(usr, argc ? arg(0) : txt);
    } else if (seq(cmd, "MODE")) {
        h_mode((char *)arg(0), txt);
    } else if (seq(cmd, "KICK")) {
        h_kick(usr, (char *)arg(0), arg(1), txt);
    } else if (seq(cmd, "TOPIC")) {
        h_topic(usr, (char *)arg(0), txt);
    } else if (cmd[0] >= '0' && cmd[0] <= '9') {
        h_numeric(to_u16(cmd), txt);
    } else {
        win_print((i8)wcur, par);     /* unknown: show raw-ish */
    }
}

static void parse_line(char *line) {
    char *usr = "", *cmd, *par, *txt = "", *p = line, *q, *b;
    if (!*p) return;
    if (*p == '@') { while (*p && *p != ' ') p++; while (*p == ' ') p++; }   /* skip IRCv3 tags */
    if (*p == ':') {
        usr = ++p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
        while (*p == ' ') p++;
        b = usr; while (*b && *b != '!') b++; if (*b) *b = 0;                /* nick before '!' */
    }
    cmd = p;
    while (*p && *p != ' ') p++;
    if (*p) *p++ = 0;
    while (*p == ' ') p++;
    par = p;
    /* split trailing text (" :" or leading ':') */
    q = par;
    if (*q == ':') { txt = q + 1; *q = 0; }
    else { while (*q) { if (q[0] == ' ' && q[1] == ':') { *q = 0; txt = q + 2; break; } q++; } }
    dispatch(usr, cmd, par, txt);
}

/* ---- feed ---------------------------------------------------------- */
static char asmbuf[512];
static u16  asmpos;
void irc_feed(const u8 *data, u16 n) {
    u16 i;
    if (n) { idle_s = 0; ka_pinged = 0; }    /* RX activity resets the keepalive timer */
    for (i = 0; i < n; i++) {
        u8 c = data[i];
        if (c == '\n') {
            asmbuf[asmpos] = 0;
            if (enc_on) utf8_to_cp866(asmbuf);   /* incoming UTF-8 -> CP866 */
            parse_line(asmbuf);
            asmpos = 0;
        }
        else if (c != '\r' && asmpos < sizeof(asmbuf) - 1) asmbuf[asmpos++] = (char)c;
    }
}

/* ---- public -------------------------------------------------------- */
void irc_init(const char *nick) {
    u8 i;
    for (i = 0; i < MAX_WIN; i++) { win[i].flags = 0; win[i].name[0] = 0; win[i].users = 0; }
    hist_init();
    s_cpy(mynick, nick, sizeof(mynick));
    s_cpy(win[0].name, "(server)", WIN_NAME);
    win[0].flags = F_ACTIVE | F_SERVER;
    hist_open(0);
    wcur = 0; registered = 0; asmpos = 0;
    banner_refresh();
    status_refresh();
}

static void irc_register(void) {
    ns_done = 0;                 /* allow auto-identify once on this connection */
    net_warn = 0;                /* fresh connection: clear any stale link warning */
    idle_s = 0; ka_pinged = 0; link_up = 1;   /* arm the keepalive/timeout watchdog */
    net_send("NICK "); net_send(mynick); net_send("\r\n");
    net_send("USER sprintalk 0 * :" APP_TITLE "\r\n");
}

i8 irc_connect(const char *host, const char *port) {
    i8 r = net_connect(host, port);
    if (r == NET_OK) irc_register();
    return r;
}

u8 irc_connected(void) { return net_is_connected(); }

/* One-shot link teardown: note it, drop registration, refresh status. Guarded by
 * link_up so it fires once even if both keepalive and the main loop notice. */
static void link_down(const char *why) {
    if (!link_up) return;
    link_up = 0;
    registered = 0;
    net_warn = 0;
    win_print(0, why);
    status_refresh();
}

/* Called by the main loop when net_poll reports the link dropped (ESP "CLOSED"). */
void irc_on_disconnect(void) {
    link_down("* Disconnected from server (connection closed)");
}

/* Keepalive + dead-link timeout. Call once per main-loop pass; self-throttles to
 * once per second. After KA_PING_S of silence we PING the server; if there is
 * still no reply by KA_TIMEOUT_S we close the (apparently dead) link. Any RX
 * resets the timer (see irc_feed). */
void irc_keepalive(void) {
    static u8 last_s = 0xFF;
    dss_time_t t;
    dss_gettime(&t);
    if (t.second == last_s) return;          /* once per second */
    last_s = t.second;
    if (!link_up) return;
    if (idle_s < 0xFFFF) idle_s++;
    if (idle_s >= KA_PING_S && !ka_pinged) {
        ka_pinged = 1;
        net_send("PING :keepalive\r\n");
    }
    if (idle_s >= KA_TIMEOUT_S) {
        net_close();
        link_down("* Connection timed out (no data from server)");
    }
}

const char *irc_nick_str(void) { return mynick; }

void irc_local(const char *line) { win_print((i8)wcur, line); }   /* local note -> current window */

void irc_set_nick(const char *n) {
    s_cpy(mynick, n, sizeof(mynick));
    if (net_is_connected()) { net_send("NICK "); net_send(mynick); net_send("\r\n"); }
    status_refresh();
}

void irc_join(const char *chan) {
    net_send("JOIN "); net_send(chan); net_send("\r\n");
}

void irc_part(void) {
    if (wcur == 0) return;
    net_send("PART "); net_send(win[wcur].name); net_send("\r\n");
    win_close(wcur);
}

/* /close: close the current window. PART it first if it's a channel; a query
 * window (or the server window stays) just closes locally. */
void irc_close(void) {
    if (wcur == 0) { term_notif("cannot close the server window"); return; }
    if ((win[wcur].name[0] == '#' || win[wcur].name[0] == '&') && net_is_connected()) {
        net_send("PART "); net_send(win[wcur].name); net_send("\r\n");
    }
    win_close(wcur);
}

void irc_say(const char *text) {
    if (wcur == 0 || !net_is_connected()) { term_notif("join a channel first (/join #chan)"); return; }
    {
        const char *sendtext = text;
        if (enc_on) { cp866_to_utf8(text, sendbuf, sizeof(sendbuf)); sendtext = sendbuf; }
        net_send("PRIVMSG "); net_send(win[wcur].name); net_send(" :"); net_send(sendtext); net_send("\r\n");
    }
    o_init(); o_c('<'); o_str(mynick); o_str("> "); o_str(text); o_end();   /* echo as typed (CP866) */
    win_print((i8)wcur, out);
}

/* ---- NickServ ------------------------------------------------------ */
void irc_set_nspass(const char *p) { s_cpy(nspass, p ? p : "", sizeof(nspass)); }
const char *irc_nspass(void) { return nspass; }

/* Send NickServ IDENTIFY. pass NULL/empty -> use the stored password. */
void irc_identify(const char *pass) {
    if (!pass || !pass[0]) pass = nspass;
    if (!pass[0]) { term_notif("no password (use /pass <password> first)"); return; }
    if (!net_is_connected()) { term_notif("not connected"); return; }
    net_send("PRIVMSG NickServ :IDENTIFY "); net_send(pass); net_send("\r\n");
    ns_done = 1;
    win_print(0, "* identifying with NickServ...");
}

/* /me <action>: CTCP ACTION to the current window (mirrors received ACTION). */
void irc_me(const char *text) {
    if (wcur == 0 || !net_is_connected()) { term_notif("join a channel first (/join #chan)"); return; }
    {
        const char *sendtext = text;
        if (enc_on) { cp866_to_utf8(text, sendbuf, sizeof(sendbuf)); sendtext = sendbuf; }
        net_send("PRIVMSG "); net_send(win[wcur].name); net_send(" :\001ACTION ");
        net_send(sendtext); net_send("\001\r\n");
    }
    o_init(); o_str("* "); o_str(mynick); o_c(' '); o_str(text); o_end();   /* local echo (CP866) */
    win_print((i8)wcur, out);
}

/* /query <nick>: open (or switch to) an empty query window for nick. */
void irc_query(const char *nick) {
    i8 w;
    if (!nick[0]) return;
    if (!net_is_connected()) { term_notif("not connected"); return; }
    w = win_find(nick);
    if (w < 0) w = win_add(nick, F_QUERY);
    if (w < 0) { win_print(0, "* cannot open window (max reached)"); return; }
    win_switch((u8)w);
}

/* /msg <target> <text>: send a PRIVMSG to an arbitrary target (nick or channel).
 * Mirrors the original: a nick target auto-opens its query window; the message
 * is echoed into the target's window (the correct place for history), with a
 * notif if that window isn't the current one so the sender still gets feedback. */
void irc_msg(const char *target, const char *text) {
    i8 w;
    if (!target[0] || !text[0]) return;
    if (!net_is_connected()) { term_notif("not connected"); return; }
    {
        const char *sendtext = text;
        if (enc_on) { cp866_to_utf8(text, sendbuf, sizeof(sendbuf)); sendtext = sendbuf; }
        net_send("PRIVMSG "); net_send(target); net_send(" :"); net_send(sendtext); net_send("\r\n");
    }
    if (target[0] == '#' || target[0] == '&') {
        w = win_find(target);
    } else {
        w = win_find(target);
        if (w < 0) w = win_add(target, F_QUERY);
    }
    if (w < 0) w = 0;
    o_init(); o_c('<'); o_str(mynick); o_str("> "); o_str(text); o_end();   /* echo as typed (CP866) */
    win_print(w, out);
    if ((u8)w != wcur) { o_init(); o_str("-> "); o_str(target); o_end(); term_notif(out); }
}

void irc_scroll_up(void)   { hist_scroll(wcur, -1); }
void irc_scroll_down(void) { hist_scroll(wcur, 1); }

void irc_toggle_ts(void) {
    ts_on = !ts_on;
    irc_local(ts_on ? "* timestamps on" : "* timestamps off");
}

void irc_toggle_encoding(void) {
    enc_on = !enc_on;
    irc_local(enc_on ? "* encoding: UTF-8 <-> CP866" : "* encoding: raw (no recoding)");
}

void irc_ignore(const char *nick) {
    u8 i;
    if (!nick[0]) {                              /* list current ignores */
        irc_local("* ignore list:");
        for (i = 0; i < MAX_IGN; i++) if (ign[i][0]) irc_local(ign[i]);
        return;
    }
    for (i = 0; i < MAX_IGN; i++)                /* toggle off if present */
        if (ign[i][0] && ieq(ign[i], nick)) { ign[i][0] = 0; o_init(); o_str("* un-ignored "); o_str(nick); o_end(); irc_local(out); return; }
    for (i = 0; i < MAX_IGN; i++)                /* else add */
        if (!ign[i][0]) { s_cpy(ign[i], nick, sizeof(ign[0])); o_init(); o_str("* ignoring "); o_str(nick); o_end(); irc_local(out); return; }
    irc_local("* ignore list full");
}

void irc_away(const char *msg) {
    if (msg[0]) { net_send("AWAY :"); net_send(msg); net_send("\r\n"); irc_local("* you are now away"); }
    else        { net_send("AWAY\r\n"); irc_local("* you are back"); }
}

void irc_raw(const char *line) { net_send(line); net_send("\r\n"); }

/* forward an unrecognized "/cmd args" to the server as a raw IRC command */
void irc_send_cmd(const char *cmd, const char *arg) {
    if (!net_is_connected()) { term_notif("not connected"); return; }
    net_send(cmd);
    if (arg[0]) { net_send(" "); net_send(arg); }
    net_send("\r\n");
}

void irc_quit(void) {
    if (net_is_connected()) { net_send("QUIT :" APP_TITLE "\r\n"); net_close(); }
    status_refresh();
}

static void cycle(i8 dir) {
    u8 i, idx = wcur;
    for (i = 0; i < MAX_WIN; i++) {
        idx = (u8)((idx + (dir > 0 ? 1 : MAX_WIN - 1)) % MAX_WIN);
        if (win[idx].flags & F_ACTIVE) { win_switch(idx); return; }
    }
}
void irc_next_window(void) { cycle(1); }
void irc_prev_window(void) { cycle(-1); }
