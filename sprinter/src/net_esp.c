/*
 * net_esp.c — ESP (SprinterWiFi) transport behind net.h.
 * Refactored from the proven Stage-4 netirc flow: single-connection transparent
 * mode (CIPMUX=0, CIPMODE=1). Assumes NETUP already brought Wi-Fi up.
 */
#include "net.h"
#include "netcfg.h"
#include "isauart.h"
#include "uart.h"

static u8 connected;
static u8 scratch[256];

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
    quiet(2);
    uart_tx_str("+++");
    quiet(2);
    uart_tx_str("\r\n");
    quiet(1);
    at("ATE0", 1);
    at("AT+CIPCLOSE", 1);
}

i8 net_init(void) {
    netcfg_t cfg;
    connected = 0;
    if (!uart_probe()) return NET_NO_HW;
    netcfg_load(&cfg);
    uart_init(uart_divisor(cfg.baud));
    esp_reset();
    return NET_OK;
}

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
