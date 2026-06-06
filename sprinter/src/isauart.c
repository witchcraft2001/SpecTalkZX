/*
 * isauart.c — ISA window open/close + TL16C550 presence probe. See isauart.h.
 * Port of the open/close sequence from sprinter_wifi .../lib/isa.asm:
 *   PAGE3 port = 0xE2, PORT_SYSTEM = 0x1FFD, PORT_ISA = 0x9FBD.
 * Slot select byte = (slot << 1) | 0xD4: slot0 -> 0xD4 (ISA1), slot1 -> 0xD6 (ISA2).
 */
#include "isauart.h"

static u8 isa_saved_win3;        /* previous WIN3 page, restored by isa_close */
static u8 isa_slot_sel = 0xD4;   /* PAGE3 select byte for the active ISA slot */
static u8 isa_slot_idx;          /* 0/1 mirror of isa_slot_sel (diagnostic) */

void isa_set_slot(u8 slot) {
    isa_slot_idx = (u8)(slot & 1);
    isa_slot_sel = (u8)((isa_slot_idx << 1) | 0xD4);
}

u8 isa_get_slot(void) { return isa_slot_idx; }

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
        ld      a, (_isa_slot_sel) ; slot select byte (0xD4=ISA1 / 0xD6=ISA2)
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

/* Probe the slot already selected by isa_set_slot. Same checks as UART_FIND:
 * the IER high nibble must read 0 (a floating/empty slot reads 0xFF), then the
 * scratch register must hold both 0x55 and 0xAA. */
static u8 probe_current(void) {
    u8 ok;
    isa_open();
    ok = ((*((volatile u8 *)UART_IER) & 0xF0) == 0);
    if (ok) {
        *((volatile u8 *)UART_SCR) = 0x55;
        ok = (*((volatile u8 *)UART_SCR) == 0x55);
    }
    if (ok) {
        *((volatile u8 *)UART_SCR) = 0xAA;
        ok = (*((volatile u8 *)UART_SCR) == 0xAA);
    }
    isa_close();
    return ok;
}

u8 uart_probe(void) {
    if (probe_current()) return 1;          /* honour the NETUP-published slot first */
    isa_set_slot(0); if (probe_current()) return 1;
    isa_set_slot(1); if (probe_current()) return 1;
    isa_set_slot(0);                        /* none found: leave the default selected */
    return 0;
}
