/*
 * isauart.h — ISA window discipline + TL16C550 UART access for SprinterWiFi (ESP).
 *
 * The UART (and any ISA card) is memory-mapped into WIN3 (0xC000-0xFFFF). isa_open()
 * remaps WIN3 to the ISA space (saving the previous WIN3 page); isa_close() restores
 * it. DSS/BIOS also use WIN3, so: keep ISA open ONLY around a register/data burst and
 * never call DSS/BIOS while it is open. This program's code+data+stack live entirely
 * in WIN1, so swapping WIN3 does not disturb them.
 *
 * TL16C550 register map (ISA base 0xC000 + COM3 0x03E8 = 0xC3E8):
 *   RBR/THR/DLL 0xC3E8  IER/DLM 0xC3E9  IIR/FCR 0xC3EA  LCR 0xC3EB
 *   MCR 0xC3EC  LSR 0xC3ED  MSR 0xC3EE  SCR 0xC3EF
 */
#ifndef ISAUART_H
#define ISAUART_H

#include <sprinter.h>

#define UART_BASE 0xC3E8
#define UART_IER  0xC3E9   /* interrupt-enable register — hi nibble reads 0 on a real UART */
#define UART_SCR  0xC3EF   /* scratch register — used for presence test */

void isa_open(void);       /* remap WIN3 to ISA (saves prev page) — no DSS calls until close */
void isa_close(void);      /* restore WIN3 */

/* Select which ISA slot the UART lives in (0 -> ISA1/0xD4, 1 -> ISA2/0xD6).
 * isa_open() maps that slot from then on. NETUP publishes the detected slot in
 * the NET_ESP_HW env var ("<slot>/#3E8"); net_init() feeds it here. */
void isa_set_slot(u8 slot);
u8   isa_get_slot(void);   /* slot index currently selected (diagnostic) */

/* Self-contained presence test (IER hi-nibble + scratch-register write/readback),
 * mirroring the network lib's UART_FIND. Probes the currently-selected slot first,
 * then scans slot 0 and slot 1; on success leaves that slot selected and returns 1.
 * Returns 0 if no TL16C550 responds in any slot. Safe to call between DSS calls. */
u8 uart_probe(void);

#endif /* ISAUART_H */
