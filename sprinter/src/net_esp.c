/*
 * net_esp.c — ESP (SprinterWiFi) transport behind net.h.
 * Refactored from the proven Stage-4 netirc flow: single-connection transparent
 * mode (CIPMUX=0, CIPMODE=1). Assumes NETUP already brought Wi-Fi up.
 */
#include "net.h"
#include "isauart.h"
#include "uart.h"

static u8 connected;
static u8 scratch[256];
static char g_baud[8];      /* NET.CFG BAUD as parsed (diagnostic) */
static u8   g_div;          /* divisor actually applied */

/* ~n seconds of silence (no TX), draining/discarding RX — guard for "+++". */
static void quiet(u8 n) {
    dss_time_t t;
    u8 last, k = 0;
    dss_gettime(&t); last = t.second;
    while (k < n) {
        uart_drain(scratch, sizeof(scratch));
        dss_gettime(&t);
        if (t.second != last) { last = t.second; k++; }
    }
}

static void at(const char *cmd, u8 secs) {
    uart_tx_str(cmd);
    uart_tx_str("\r\n");
    quiet(secs);
}

/* back to command mode + drop any leftover socket */
static void esp_reset(void) {
    quiet(1);                 /* silence before escape (guard ~1s, plenty) */
    uart_tx_str("+++");
    quiet(1);                 /* silence after escape */
    uart_tx_str("\r\n");      /* flush any partial command-mode buffer */
    at("ATE0", 1);
    at("AT+CIPCLOSE", 1);
}

/* case-insensitive equality */
static u8 ci_eq(const char *a, const char *b) {
    while (*a && *b) {
        u8 ca = (*a >= 'a' && *a <= 'z') ? (u8)(*a - 32) : (u8)*a;
        u8 cb = (*b >= 'a' && *b <= 'z') ? (u8)(*b - 32) : (u8)*b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

i8 net_init(void) {
    char v[12];
    connected = 0;
    /* The SprinterWiFi kit (NETUP) publishes the live network state in DSS env
     * vars; NET=WIFI marks the link is up. Read NET_BAUD for the actual baud
     * NETUP applied (no NET.CFG parsing needed). */
    if (dss_getenv("NET", v) != 0 || !ci_eq(v, "WIFI")) return NET_NO_LINK;
    if (!uart_probe()) return NET_NO_HW;
    if (dss_getenv("NET_BAUD", g_baud) != 0) g_baud[0] = 0;
    g_div = uart_divisor(g_baud);
    uart_init(g_div);
    esp_reset();
    return NET_OK;
}

const char *net_cfg_baud(void) { return g_baud; }
u8 net_cfg_div(void) { return g_div; }

i8 net_connect(const char *host, const char *port) {
    esp_reset();
    at("AT+CIPMUX=0", 1);
    at("AT+CIPMODE=1", 1);
    /* AT+CIPSTART="TCP","<host>",<port> */
    uart_tx_str("AT+CIPSTART=\"TCP\",\"");
    uart_tx_str(host);
    uart_tx_str("\",");
    uart_tx_str(port);
    uart_tx_str("\r\n");
    quiet(6);                 /* DNS resolve + TCP connect */
    at("AT+CIPSEND", 2);      /* ">" prompt -> raw byte pipe */
    connected = 1;
    return NET_OK;
}

void net_send(const char *s) {
    uart_tx_str(s);
}

u16 net_poll(u8 *buf, u16 max) {
    return uart_drain(buf, max);
}

void net_close(void) {
    if (connected) { esp_reset(); connected = 0; }
}

u8 net_is_connected(void) { return connected; }
