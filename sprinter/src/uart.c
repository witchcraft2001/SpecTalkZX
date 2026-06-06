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

#define MCR_AFE  0x20    /* auto flow control enable */
#define MCR_RTS  0x02    /* request to send (asserted = ESP may transmit) */

#define TX_GUARD 30000   /* THRE spin limit (~tens of ms) before declaring a stall */

/* Set when a TX could not complete (ESP wedged / hardware flow held off). The
 * old code spun ~64K then wrote anyway, freezing seconds per line; now we abort
 * the line and raise this so the UI can warn instead of hanging silently. */
u8 uart_tx_stall;
u8 uart_stalled(void) { return uart_tx_stall; }
void uart_clear_stall(void) { uart_tx_stall = 0; }

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
    isa_open();
    /* FIFO on + RX trigger level 4 (FCR bits7:6 = 01). With AFE auto-flow the UART
     * deasserts RTS once >=4 bytes sit in the RX FIFO, so during a slow render (when
     * net_poll isn't draining) RTS stays low and the ESP (flow=3, set by NETUP) is
     * held off for the WHOLE render — no overrun, with 12 bytes of in-flight headroom.
     * Trigger 8 left only 8 bytes and a slow-reacting ESP overran (lost MOTD chunks);
     * trigger 1 fixed that but throttled bursts, so 4 is the speed/safety compromise.
     * We do NOT toggle RTS manually: forcing MCR bit1 low wedged RX on real HW;
     * letting AFE drive RTS off the FIFO level is the mechanism that already worked. */
    *U_FCR = 0x47;          /* FIFO enable + flush RX/TX + RX trigger 4 */
    *U_IER = 0x00;          /* no interrupts */
    *U_LCR = 0x83;          /* DLAB | 8N1 */
    *U_DLL = divisor;
    *U_DLM = 0x00;
    *U_LCR = 0x03;          /* 8N1, DLAB off */
    *U_MCR = MCR_AFE | MCR_RTS;
    isa_close();
}

void uart_tx_str(const char *s) {
    isa_open();
    while (*s) {
        u16 guard = 0;
        while (!(*U_LSR & LSR_THRE)) {
            if (++guard >= TX_GUARD) { uart_tx_stall = 1; isa_close(); return; }  /* abort, don't hang */
        }
        *U_THR = (u8)*s++;
    }
    isa_close();
}

u16 uart_drain(u8 *buf, u16 max) {
    u16 n = 0;
    isa_open();
    while (n < max && (*U_LSR & LSR_DR)) buf[n++] = *U_RBR;
    isa_close();
    return n;
}
