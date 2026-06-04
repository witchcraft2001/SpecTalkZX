/*
 * netdiag.c — SpecTalk Sprinter port, Stage 3 diagnostics.
 *
 * Validates the networking foundations the IRC client will rely on:
 *   - read %NET_DIR% and load NET.CFG, show the parsed connection settings
 *   - exercise the ISA/WIN3 open-close discipline and probe the TL16C550 UART
 *
 * The UART probe needs the SprinterWiFi (ESP) card; on an emulator without it,
 * "not detected" is the expected, non-fatal result.
 *
 * Copyright (C) 2026 M. Ignacio Monge Garcia — GPLv2 (see ../../LICENSE)
 */

#include <sprinter.h>
#include "netcfg.h"
#include "isauart.h"

static netcfg_t cfg;   /* static: avoids a large stack frame */

static void line(const char *s) { dss_puts(s); dss_puts("\r\n"); }

static void kv(const char *label, const char *value) {
    dss_puts(label);
    dss_puts(value && value[0] ? value : "(empty)");
    dss_puts("\r\n");
}

void main(void) {
    i8 rc;
    u8 uart;

    dss_clrscr();
    line("=== SpecTalk ZX  --  Sprinter port  --  Stage 3: NET diagnostics ===");
    line("");

    rc = netcfg_load(&cfg);

    kv("NET_DIR      : ", cfg.net_dir);
    kv("Config path  : ", cfg.path);
    if (rc == 0) {
        line("NET.CFG      : loaded OK");
        line("");
        kv("  SSID       : ", cfg.ssid);
        kv("  PASS       : ", cfg.pass[0] ? "(set)" : "");
        kv("  DHCP       : ", cfg.dhcp);
        kv("  IP         : ", cfg.ip);
        kv("  GATEWAY    : ", cfg.gateway);
        kv("  NETMASK    : ", cfg.netmask);
        kv("  DNS1       : ", cfg.dns1);
        kv("  DNS2       : ", cfg.dns2);
        kv("  NTP        : ", cfg.ntp);
        kv("  BAUD       : ", cfg.baud);
    } else if (rc == -1) {
        line("NET.CFG      : NOT FOUND (set %NET_DIR% or place NET.CFG here)");
    } else {
        line("NET.CFG      : read error");
    }

    line("");
    line("--- ISA / TL16C550 UART probe @ 0xC3E8 (WIN3) ---");
    uart = uart_probe();    /* opens ISA, scratch test, closes ISA, restores WIN3 */
    if (uart)
        line("UART         : PRESENT (scratch register responded)");
    else
        line("UART         : not detected (no ESP card / emulator) -- expected off HW");
    line("ISA window   : opened and closed; WIN3 restored");

    line("");
    line("Press any key to exit.");
    dss_waitkey();
    dss_exit(0);
}
