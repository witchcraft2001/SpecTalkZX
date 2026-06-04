/*
 * isauart.c — ISA window open/close + TL16C550 presence probe. See isauart.h.
 * Port of the open/close sequence from sprinter_wifi .../lib/isa.asm:
 *   PAGE3 port = 0xE2, PORT_SYSTEM = 0x1FFD, PORT_ISA = 0x9FBD, ISA1 slot = 0xD4.
 */
#include "isauart.h"

static u8 isa_saved_win3;   /* previous WIN3 page, restored by isa_close */

void isa_open(void) __naked {
    __asm
        push    af
        push    bc
        ld      bc, #0x00e2     ; PAGE3 page-select port
        in      a, (c)
        ld      (_isa_saved_win3), a
        ld      bc, #0x1ffd     ; PORT_SYSTEM
        ld      a, #0x11
        out     (c), a
        ld      a, #0xd4        ; (slot0 << 1) | 0xD4 = ISA1
        ld      bc, #0x00e2     ; PAGE3 -> map WIN3 to ISA
        out     (c), a
        ld      bc, #0x9fbd     ; PORT_ISA
        xor     a, a
        out     (c), a
        pop     bc
        pop     af
        ret
    __endasm;
}

void isa_close(void) __naked {
    __asm
        push    af
        push    bc
        ld      a, #0x01
        ld      bc, #0x1ffd     ; PORT_SYSTEM
        out     (c), a
        ld      bc, #0x00e2     ; PAGE3
        ld      a, (_isa_saved_win3)
        out     (c), a
        pop     bc
        pop     af
        ret
    __endasm;
}

u8 uart_probe(void) {
    u8 ok;
    isa_open();
    /* scratch-register write/read-back: a real TL16C550 holds the value */
    *((volatile u8 *)UART_SCR) = 0x55;
    ok = (*((volatile u8 *)UART_SCR) == 0x55);
    if (ok) {
        *((volatile u8 *)UART_SCR) = 0xAA;
        ok = (*((volatile u8 *)UART_SCR) == 0xAA);
    }
    isa_close();
    return ok;
}
