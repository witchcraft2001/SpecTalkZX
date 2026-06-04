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
#define UART_SCR  0xC3EF   /* scratch register — used for presence test */

void isa_open(void);       /* remap WIN3 to ISA (saves prev page) — no DSS calls until close */
void isa_close(void);      /* restore WIN3 */

/* Self-contained: opens ISA, runs the scratch-register presence test, closes ISA.
 * Returns 1 if a TL16C550 responds, 0 otherwise. Safe to call between DSS calls. */
u8 uart_probe(void);

#endif /* ISAUART_H */
