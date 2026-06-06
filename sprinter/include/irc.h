/*
 * irc.h — IRC engine: window model + line parser/dispatch + protocol handlers.
 *
 * Adapted from the original SpecTalk irc_handlers.c, de-ZX'd: clean C, renders
 * through term_*, sends through net_*, no z88dk ABI. Up to MAX_WIN windows
 * (windows[0] = server/status). The current window is shown in the chat area;
 * others raise an activity flag. (Per-window paged history is a planned follow-up;
 * the Window struct reserves a slot for the DSS page id.)
 */
#ifndef IRC_H
#define IRC_H

#include <sprinter.h>

#define MAX_WIN  10

void irc_init(const char *nick);

/* transport-facing */
void irc_feed(const u8 *data, u16 n);     /* assemble lines from the byte stream, dispatch */
i8   irc_connect(const char *host, const char *port);  /* net_connect + register */
u8   irc_connected(void);
void irc_on_disconnect(void);             /* main loop calls this when the link drops */
void irc_net_warn(u8 on);                 /* show/clear an "ESP not responding" status marker */

/* user commands (called from the input layer) */
void irc_set_nick(const char *n);
const char *irc_nick_str(void);            /* current nick (for saving settings) */
void irc_local(const char *line);          /* show a local note in the current window */
void irc_join(const char *chan);
void irc_part(void);
void irc_say(const char *text);            /* PRIVMSG to current window */
void irc_me(const char *text);             /* CTCP ACTION to current window */
void irc_query(const char *nick);          /* open/switch to a query window */
void irc_msg(const char *target, const char *text);  /* PRIVMSG to an arbitrary target */
void irc_raw(const char *line);
void irc_send_cmd(const char *cmd, const char *arg);   /* forward "CMD args" to server */
void irc_quit(void);

/* window navigation */
void irc_next_window(void);
void irc_prev_window(void);
void irc_scroll_up(void);     /* PgUp: older history in the current window */
void irc_scroll_down(void);   /* PgDn: newer history / back to live */

/* NickServ */
void irc_identify(const char *pass);     /* PRIVMSG NickServ :IDENTIFY (empty = stored pass) */
void irc_set_nspass(const char *p);      /* store NickServ password (for cfg + auto-id) */
const char *irc_nspass(void);            /* stored password (for saving to cfg) */

void irc_toggle_ts(void);            /* toggle message timestamps */
void irc_toggle_encoding(void);      /* toggle UTF-8 <-> CP866 recoding */
void irc_ignore(const char *nick);   /* toggle ignoring a nick (empty = list) */
void irc_away(const char *msg);      /* set away (empty = back) */

#endif /* IRC_H */
