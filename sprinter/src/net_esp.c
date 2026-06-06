/*
 * net_esp.c — ESP (SprinterWiFi) transport behind net.h.
 * Refactored from the proven Stage-4 netirc flow: single-connection transparent
 * mode (CIPMUX=0, CIPMODE=1). Assumes NETUP already brought Wi-Fi up.
 */
#include "net.h"
#include "isauart.h"
#include "uart.h"
#include "term.h"

static u8 connected;
static u8 ever_used;        /* we put the ESP into transparent mode at least once */
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
        if (t.second != last) { last = t.second; k++; term_clock(); }  /* keep the clock alive during waits */
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

/* Hand the ESP back the way the SprinterWiFi tools (ftp/wget/...) expect it:
 * out of transparent mode, socket closed, and CRUCIALLY CIPMODE=0 (normal mode).
 * We put it in CIPMODE=1; if we don't undo that, their AT commands fail with
 * "ESP communication error #1". Echo stays off (ATE0) — the kit uses ATE0 too. */
static void esp_restore(void) {
    quiet(1);
    uart_tx_str("+++");       /* escape transparent mode if still in it */
    quiet(1);
    uart_tx_str("\r\n");
    at("ATE0", 1);
    at("AT+CIPCLOSE", 1);     /* close any socket (harmless if none) */
    at("AT+CIPMODE=0", 1);    /* <-- restore normal mode for the next program */
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
    /* NETUP records the detected ISA slot in NET_ESP_HW = "<slot>/#3E8". Honour it
     * so we map the right slot (the card need not be in ISA1); uart_probe still
     * scans both slots as a fallback if the hint is missing or wrong. */
    {
        char hw[16];
        if (dss_getenv("NET_ESP_HW", hw) == 0 && hw[0] >= '0' && hw[0] <= '9')
            isa_set_slot((u8)(hw[0] - '0'));
    }
    if (!uart_probe()) return NET_NO_HW;
    if (dss_getenv("NET_BAUD", g_baud) != 0) g_baud[0] = 0;
    g_div = uart_divisor(g_baud);
    uart_init(g_div);
    esp_reset();
    return NET_OK;
}

const char *net_cfg_baud(void) { return g_baud; }
u8 net_cfg_div(void) { return g_div; }
u8 net_cfg_slot(void) { return isa_get_slot(); }

/* When the remote closes the socket, ESP-AT leaves transparent mode and emits a
 * "\r\nCLOSED\r\n" line on the UART. We watch the RX stream for that token (a
 * state machine that survives chunk boundaries) and clear `connected`. Anchoring
 * on the leading CR/LF keeps an IRC message that merely contains "CLOSED" from
 * tripping it (message text never starts a line with that word). */
static const u8 CLOSED_TOK[] = { 0x0D, 0x0A, 'C', 'L', 'O', 'S', 'E', 'D' };
static u8 cm;                 /* how many leading bytes of CLOSED_TOK matched so far */

static void watch_closed(const u8 *buf, u16 n) {
    u16 i;
    for (i = 0; i < n; i++) {
        if (buf[i] == CLOSED_TOK[cm]) {
            if (++cm == sizeof(CLOSED_TOK)) { connected = 0; cm = 0; }
        } else {
            cm = (buf[i] == CLOSED_TOK[0]) ? 1 : 0;
        }
    }
}

i8 net_connect(const char *host, const char *port) {
    esp_reset();
    cm = 0;
    ever_used = 1;            /* from here the ESP needs CIPMODE restored on exit */
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
    u16 n = uart_drain(buf, max);
    if (connected && n) watch_closed(buf, n);
    return n;
}

/* Close the link. Restores the ESP whenever we ever entered transparent mode —
 * even if the socket is already gone (timeout/CLOSED), so CIPMODE is always put
 * back to 0 before we hand the card to the next program. */
void net_close(void) {
    if (ever_used) esp_restore();
    connected = 0;
}

u8 net_is_connected(void) { return connected; }

u8 net_stalled(void) { return uart_stalled(); }
void net_clear_stall(void) { uart_clear_stall(); }
