/*
 * net.h — transport abstraction for the IRC client.
 *
 * Backend-agnostic by design (ESP now; NE2000 later behind the same API). The ESP
 * backend uses transparent mode, so the link is a raw byte pipe: net_send writes
 * raw bytes, net_poll reads whatever has arrived (non-blocking). IRC framing
 * (lines, PING/PONG) is the caller's job.
 */
#ifndef NET_H
#define NET_H

#include <sprinter.h>

#define NET_NO_HW   -1     /* no TL16C550 UART present */
#define NET_NO_LINK -2     /* Wi-Fi not up (NETUP not run): env NET != WIFI */
#define NET_OK       0

i8   net_init(void);                                  /* probe+init UART, ESP to cmd mode; NET_NO_HW if absent */
i8   net_connect(const char *host, const char *port); /* open TCP, enter transparent mode */
void net_send(const char *s);                         /* send raw bytes (NUL-terminated) */
void net_send_parts(const char *a, const char *b, const char *c, const char *d, const char *e);
u16  net_poll(u8 *buf, u16 max);                       /* non-blocking: bytes available now */
void net_close(void);                                  /* escape transparent mode, close socket */
u8   net_is_connected(void);
u8   net_stalled(void);            /* 1 if a send recently failed (ESP not draining TX) */
void net_clear_stall(void);        /* acknowledge/clear the stall flag */
u8   net_overrun(void);            /* 1 if RX bytes were lost (overrun) since last clear */
void net_clear_overrun(void);      /* acknowledge/clear the overrun flag */
const char *net_cfg_baud(void);   /* parsed NET.CFG BAUD (diagnostic) */
u8   net_cfg_div(void);            /* UART divisor actually applied */
u8   net_cfg_slot(void);          /* ISA slot the UART was found in (0/1) */

#endif /* NET_H */
