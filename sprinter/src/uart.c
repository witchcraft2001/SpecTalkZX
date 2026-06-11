/*
 * uart.c — TL16C550 UART driver. See uart.h.
 * Register init mirrors WIFI.UART_INIT in sprinter_wifi .../lib/esplib.asm.
 */
#include "uart.h"
#include "isauart.h"

/* TL16C550 registers (ISA base 0xC3E8). DLL/DLM alias THR/IER when DLAB=1. */
#define U_THR ((volatile u8 *)0xC3E8)
#define U_RBR ((volatile u8 *)0xC3E8)
#define U_DLL ((volatile u8 *)0xC3E8)
#define U_IER ((volatile u8 *)0xC3E9)
#define U_DLM ((volatile u8 *)0xC3E9)
#define U_FCR ((volatile u8 *)0xC3EA)
#define U_LCR ((volatile u8 *)0xC3EB)
#define U_MCR ((volatile u8 *)0xC3EC)
#define U_LSR ((volatile u8 *)0xC3ED)

#define LSR_DR   0x01    /* data ready */
#define LSR_THRE 0x20    /* transmit holding register empty */
/* RX error bits in LSR: overrun / parity / framing / break. Any of these means
 * the byte stream is corrupt/misaligned and (over TCP) unrecoverable. */
#define LSR_ERRMASK 0x1E /* OE | PE | FE | BI */

#define MCR_AFE  0x20    /* auto flow control enable */
#define MCR_RTS  0x02    /* request to send (asserted = ESP may transmit) */

#define TX_GUARD 30000   /* THRE spin limit (~tens of ms) before declaring a stall */

/* Set when a TX could not complete (ESP wedged / hardware flow held off). The
 * old code spun ~64K then wrote anyway, freezing seconds per line; now we abort
 * the line and raise this so the UI can warn instead of hanging silently. */
u8 uart_tx_stall;
u8 uart_stalled(void) { return uart_tx_stall; }
void uart_clear_stall(void) { uart_tx_stall = 0; }

/* Sticky OR of LSR error bits seen since the last clear. An overrun here means
 * uart_drain lost RX bytes (the slow render outran the FIFO) -> the IRC parser
 * is now desynced; the caller resyncs/warns instead of trusting corrupt data. */
u8 uart_rx_err;
u8 uart_overrun(void) { return uart_rx_err; }
void uart_clear_overrun(void) { uart_rx_err = 0; }

static u8 streq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

/* divisor = 14745600 / (baud * 16): 115200->8 57600->16 38400->24 19200->48
 * 9600->96 230400->4. Unknown/empty -> 8 (115200). */
u8 uart_divisor(const char *baud) {
    if (streq(baud, "230400")) return 4;
    if (streq(baud, "57600"))  return 16;
    if (streq(baud, "38400"))  return 24;
    if (streq(baud, "19200"))  return 48;
    if (streq(baud, "9600"))   return 96;
    return 8;   /* 115200 and default */
}

void uart_init(u8 divisor) {
    uart_tx_stall = 0;
    uart_rx_err = 0;
    isa_open();
    /* FIFO on + RX trigger level 8 (FCR bits7:6 = 10), matching the SprinterWiFi
     * ftp/wget driver. The FIFO trigger alone is NOT relied on to survive a slow
     * render: the caller now drops RTS manually (uart_rx_pause) for the whole
     * render and raises it again (uart_rx_resume) right before draining, so the
     * ESP (flow=3, set by NETUP) is held off for the entire slow path — not just
     * the few FIFO byte-times of headroom. With the manual pause in place the
     * trigger only governs burst size between drains, so TR8 (faster bursts) is
     * safe where it overran before (that earlier overrun was the render outrunning
     * the FIFO with NO manual pause). Pausing keeps AFE enabled and only clears
     * the RTS bit — the proven recipe from esplib.asm UART_RX_PAUSE/RESUME. */
    *U_FCR = 0x87;          /* FIFO enable + flush RX/TX + RX trigger 8 */
    *U_IER = 0x00;          /* no interrupts */
    *U_LCR = 0x83;          /* DLAB | 8N1 */
    *U_DLL = divisor;
    *U_DLM = 0x00;
    *U_LCR = 0x03;          /* 8N1, DLAB off */
    *U_MCR = MCR_AFE | MCR_RTS;
    isa_close();
}

static u8 tx_one(const char *s) {
    while (s && *s) {
        u16 guard = 0;
        while (!(*U_LSR & LSR_THRE)) {
            if (++guard >= TX_GUARD) { uart_tx_stall = 1; return 0; }  /* abort, don't hang */
        }
        *U_THR = (u8)*s++;
    }
    return 1;
}

void uart_tx_str(const char *s) {
    isa_open();
    tx_one(s);
    isa_close();
}

/* Send a complete logical line in one UART burst. This avoids splitting IRC
 * commands across several net_send() calls and reports one stall for the whole
 * line instead of letting later fragments run as separate sends. */
void uart_tx_parts(const char *a, const char *b, const char *c, const char *d, const char *e) {
    isa_open();
    if (tx_one(a) && tx_one(b) && tx_one(c) && tx_one(d)) tx_one(e);
    isa_close();
}

u16 uart_drain(u8 *buf, u16 max) {
    u16 n = 0;
    u8 lsr;
    isa_open();
    /* Read LSR once per byte: it carries both Data-Ready and the sticky error
     * bits, and reading it clears OE. Accumulate any error so a render-induced
     * overrun is detectable after the fact (see uart_overrun). */
    for (;;) {
        lsr = *U_LSR;
        uart_rx_err |= (u8)(lsr & LSR_ERRMASK);
        if (n >= max || !(lsr & LSR_DR)) break;
        buf[n++] = *U_RBR;
    }
    isa_close();
    return n;
}

/* Manual RX flow control (mirrors esplib.asm UART_RX_PAUSE/RESUME). Drop RTS
 * before any slow, non-draining work (rendering): with AFE still on, clearing
 * the RTS bit deasserts the line and the ESP pauses its TX. Raise RTS again just
 * before draining. This gives the ESP the whole slow path to stop, instead of
 * only the FIFO headroom — the fix for lost messages during channel renders. */
void uart_rx_pause(void) {
    isa_open();
    *U_MCR = MCR_AFE;            /* RTS low, auto-flow still enabled */
    isa_close();
}

void uart_rx_resume(void) {
    isa_open();
    *U_MCR = MCR_AFE | MCR_RTS;  /* RTS high: ESP may transmit again */
    isa_close();
}
