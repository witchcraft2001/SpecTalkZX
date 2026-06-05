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
u16  net_poll(u8 *buf, u16 max);                       /* non-blocking: bytes available now */
void net_close(void);                                  /* escape transparent mode, close socket */
u8   net_is_connected(void);
const char *net_cfg_baud(void);   /* parsed NET.CFG BAUD (diagnostic) */
u8   net_cfg_div(void);            /* UART divisor actually applied */

#endif /* NET_H */
