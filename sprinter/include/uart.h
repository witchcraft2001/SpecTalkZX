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
void uart_tx_str(const char *s);      /* send a NUL-terminated string (blocks on THRE, bounded) */
u16  uart_drain(u8 *buf, u16 max);    /* read all currently-available RX bytes (non-blocking) */

#endif /* UART_H */
