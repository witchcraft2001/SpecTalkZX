/*
 * netirc.c — SpecTalk Sprinter port, Stage 4: ESP TCP + raw IRC handshake.
 *
 * Assumes NETUP already brought Wi-Fi up (kit model). We:
 *   - init the local TL16C550 to the NET.CFG baud
 *   - clean up any leftover ESP state (escape transparent mode, close old socket)
 *   - single-connection transparent mode (CIPMUX=0, CIPMODE=1), TCP to Libera
 *   - send NICK/USER, dump the server's raw replies, auto-PONG
 *   - on exit: QUIT, escape transparent, close — so no ghost session lingers
 *
 * Transparent mode makes the link a raw byte pipe, mirroring how the original
 * SpecTalk spoke to its ESP8266, so net_send/net_recv stay simple (Stage 5).
 *
 * Copyright (C) 2026 M. Ignacio Monge Garcia — GPLv2 (see ../../LICENSE)
 */

#include <sprinter.h>
#include "netcfg.h"
#include "isauart.h"
#include "uart.h"

#define IRC_HOST "irc.libera.chat"
#define IRC_PORT "6667"
#define IRC_NICK "SprSpecTalk"

static netcfg_t cfg;
static u8  rx[256];
static char ln[256];     /* current server line, for PING detection */
static u8  lnpos;

static void line(const char *s) { dss_puts(s); dss_puts("\r\n"); }

/* Wait ~n seconds (DSS clock ticks), draining+discarding RX, transmitting nothing.
 * Used as the silence/guard time around the "+++" transparent-mode escape. */
static void quiet(u8 n) {
    dss_time_t t;
    u8 last, k = 0;
    dss_gettime(&t); last = t.second;
    while (k < n) {
        uart_drain(rx, sizeof(rx));
        dss_gettime(&t);
        if (t.second != last) { last = t.second; k++; }
    }
}

static void maybe_pong(void) {
    if (ln[0] == 'P' && ln[1] == 'I' && ln[2] == 'N' && ln[3] == 'G') {
        uart_tx_str("PONG");
        uart_tx_str(ln + 4);   /* " :token\r" */
        uart_tx_str("\n");
    }
}

/* Drain + echo for ~secs seconds, assembling lines to auto-PONG. */
static void pump(u8 secs) {
    dss_time_t t;
    u8 last, ticks = 0, c;
    u16 i, n;
    dss_gettime(&t); last = t.second;
    while (ticks < secs) {
        n = uart_drain(rx, sizeof(rx));
        for (i = 0; i < n; i++) {
            c = rx[i];
            dss_putchar(c);
            if (c == '\n') { ln[lnpos] = 0; maybe_pong(); lnpos = 0; }
            else if (lnpos < sizeof(ln) - 1) ln[lnpos++] = c;
        }
        dss_gettime(&t);
        if (t.second != last) { last = t.second; ticks++; }
    }
}

static void cmd(const char *at, u8 secs) {
    line("");
    dss_puts(">> "); line(at);
    uart_tx_str(at);
    uart_tx_str("\r\n");
    pump(secs);
}

/* Return the ESP to command mode and drop any leftover connection. */
static void esp_reset_to_cmd(void) {
    quiet(2);                 /* silence before escape */
    uart_tx_str("+++");       /* exit transparent mode (no CR/LF!) */
    quiet(2);                 /* silence after escape */
    uart_tx_str("\r\n");      /* flush any partial command-mode buffer */
    pump(1);
    cmd("ATE0", 1);           /* disable command echo */
    cmd("AT+CIPCLOSE", 1);    /* close a leftover socket (OK, or ERROR if none) */
}

void main(void) {
    dss_clrscr();
    line("=== SpecTalk ZX -- Sprinter -- Stage 4: ESP TCP + IRC handshake ===");

    if (!uart_probe()) {
        line("ERROR: no TL16C550 UART at 0xC3E8 (need SprinterWiFi). Key to exit.");
        dss_waitkey();
        dss_exit(1);
    }
    netcfg_load(&cfg);
    uart_init(uart_divisor(cfg.baud));
    line("UART ready. Clearing any leftover ESP session...");
    lnpos = 0;

    esp_reset_to_cmd();

    cmd("AT", 1);
    cmd("AT+CIPMUX=0", 1);
    cmd("AT+CIPMODE=1", 1);
    cmd("AT+CIPSTART=\"TCP\",\"" IRC_HOST "\"," IRC_PORT, 6);  /* DNS + connect */
    cmd("AT+CIPSEND", 2);   /* ">" prompt -> raw byte pipe */

    line("");
    line(">> NICK/USER (transparent mode)");
    uart_tx_str("NICK " IRC_NICK "\r\n");
    uart_tx_str("USER spectalk 0 * :SpecTalk ZX Sprinter\r\n");

    line(">> reading server for ~20s (auto-PONG on)...");
    pump(20);

    /* clean disconnect so the nick is freed for the next run */
    uart_tx_str("QUIT :SpecTalk Sprinter\r\n");
    quiet(1);
    esp_reset_to_cmd();

    line("");
    line("--- done (session closed). Press any key to exit. ---");
    dss_waitkey();
    dss_exit(0);
}
