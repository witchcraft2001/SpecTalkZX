/*
 * uart.h — TL16C550 UART driver for SprinterWiFi (ESP), over the ISA/WIN3 window.
 *
 * Every routine brackets its register/data burst in isa_open()/isa_close() and
 * makes no DSS calls in between (see isauart.h). The local UART must be set to the
 * baud the ESP is already using (NETUP configured the ESP); for the default
 * 115200 the divisor is 8 (XIN 14.7456 MHz / (baud*16)).
 */
#ifndef UART_H
#define UART_H

#include <sprinter.h>

u8   uart_divisor(const char *baud);  /* NET.CFG BAUD string -> divisor (default 8) */
void uart_init(u8 divisor);           /* 8N1, FIFO, RTS/CTS auto-flow */
void uart_tx_str(const char *s);      /* send a NUL-terminated string; aborts on a TX stall */
u16  uart_drain(u8 *buf, u16 max);    /* read all currently-available RX bytes (non-blocking) */

void uart_rx_pause(void);             /* drop RTS (ESP holds TX) for slow non-draining work */
void uart_rx_resume(void);            /* raise RTS before draining; ESP may transmit again */

u8   uart_stalled(void);              /* 1 if a TX recently failed to drain (ESP wedged) */
void uart_clear_stall(void);          /* clear the stall flag */

u8   uart_overrun(void);              /* nonzero if an RX overrun/error was seen (data lost) */
void uart_clear_overrun(void);        /* clear the overrun flag */

#endif /* UART_H */
