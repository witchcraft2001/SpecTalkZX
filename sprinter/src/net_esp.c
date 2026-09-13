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

/* Wait for the ESP's transparent-mode ">" prompt after AT+CIPSEND, reading ONE
 * byte at a time and stopping the instant ">" is seen. Unlike quiet() (which
 * discards for a fixed time), this leaves every byte AFTER the prompt in the
 * FIFO/ESP — that tail is the server's first burst (the connect NOTICEs), which
 * a blind quiet() would have eaten, starting the IRC stream mid-line. Returns 1
 * if the prompt was seen, 0 on ~secs timeout. */
static u8 wait_prompt(u8 secs) {
    dss_time_t t;
    u8 last, k = 0, b;
    dss_gettime(&t); last = t.second;
    while (k < secs) {
        if (uart_drain(&b, 1) == 1) {
            if (b == '>') return 1;          /* prompt: leave the server tail for net_poll */
        } else {
            dss_gettime(&t);
            if (t.second != last) { last = t.second; k++; term_clock(); }
        }
    }
    return 0;
}

static u8 streq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

/* Discard whatever RX is pending right now (stale AT residue before a command).
 * Keep this strictly UART-only: /server runs inside the input path, and waiting
 * on video/DSS primitives here proved unsafe on some real machines. */
static void flush_rx(void) {
    u8 guard = 16;
    while (guard-- && uart_drain(scratch, sizeof(scratch))) ;
}

/* Wait for an AT result line, reading byte-by-byte and matching whole CRLF lines.
 * Unlike quiet() (fixed-time blind discard) this keys off the actual ESP reply,
 * so slow/backlogged responses don't leak into the IRC stream as "OK"/"ERROR"
 * noise, and a genuine failure is detected instead of assumed-success.
 * Returns: 0 = OK, 1 = ERROR/FAIL/ALREADY CONNECTED, 2 = timeout (~secs). */
#define ESP_OK      0
#define ESP_ERR     1
#define ESP_TIMEOUT 2
static u8 esp_expect(u8 secs) {
    dss_time_t t;
    u8 last, k = 0, b, rlen = 0;
    char rline[20];
    dss_gettime(&t); last = t.second;
    while (k < secs) {
        if (uart_drain(&b, 1) == 1) {
            if (b == '\r' || b == '\n') {
                rline[rlen] = 0;
                if (rlen) {
                    if (streq(rline, "OK") || streq(rline, "SEND OK")) return ESP_OK;
                    if (streq(rline, "ERROR") || streq(rline, "FAIL")
                        || streq(rline, "ALREADY CONNECTED") || streq(rline, "CLOSED"))
                        return ESP_ERR;
                }
                rlen = 0;
            } else if (rlen < sizeof(rline) - 1) {
                rline[rlen++] = (char)b;
            }
        } else {
            dss_gettime(&t);
            if (t.second != last) { last = t.second; k++; term_clock(); }
        }
    }
    return ESP_TIMEOUT;
}

/* CIPSTART is noisy on some ESP firmwares: stale ERROR/FAIL from earlier cleanup
 * can precede the real CONNECT/OK. Treat CONNECT or OK as success, ignore early
 * errors, and only fail if no success line arrives before timeout. */
static u8 esp_expect_connect(u8 secs) {
    dss_time_t t;
    u8 last, k = 0, b, rlen = 0;
    char rline[20];
    dss_gettime(&t); last = t.second;
    while (k < secs) {
        if (uart_drain(&b, 1) == 1) {
            if (b == '\r' || b == '\n') {
                rline[rlen] = 0;
                if (rlen) {
                    if (streq(rline, "CONNECT") || streq(rline, "OK")
                        || streq(rline, "ALREADY CONNECTED"))
                        return ESP_OK;
                }
                rlen = 0;
            } else if (rlen < sizeof(rline) - 1) {
                rline[rlen++] = (char)b;
            }
        } else {
            dss_gettime(&t);
            if (t.second != last) { last = t.second; k++; term_clock(); }
        }
    }
    return ESP_TIMEOUT;
}

/* back to command mode + drop any leftover socket */
static void esp_reset(void) {
    quiet(2);                 /* silence before escape (guard for "+++") */
    uart_tx_str("+++");
    quiet(2);                 /* silence after escape */
    uart_tx_str("\r\n");      /* flush any partial command-mode buffer */
    at("ATE0", 1);
    at("AT+CIPCLOSE", 1);
}

/* Hand the ESP back the way the SprinterWiFi tools (ftp/wget/...) expect it:
 * out of transparent mode, socket closed, and CRUCIALLY CIPMODE=0 (normal mode).
 * We put it in CIPMODE=1; if we don't undo that, their AT commands fail with
 * "ESP communication error #1". Echo stays off (ATE0) — the kit uses ATE0 too. */
static void esp_restore(void) {
    quiet(2);
    uart_tx_str("+++");       /* escape transparent mode if still in it */
    quiet(2);
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

    /* Each step waits for the ESP's actual reply (OK/ERROR/prompt) instead of a
     * blind fixed delay: a slow/backlogged ESP no longer leaks its responses into
     * the IRC stream, and a real failure is reported instead of assumed-success
     * (which used to wedge the UI at "connecting..." forever). CIPMUX/CIPMODE may
     * report ERROR if already set — harmless, so we don't fail on them. */
    flush_rx();
    uart_tx_str("AT+CIPMUX=0\r\n");  esp_expect(2);
    flush_rx();
    uart_tx_str("AT+CIPMODE=1\r\n"); esp_expect(2);

    flush_rx();
    uart_tx_parts("AT+CIPSTART=\"TCP\",\"", host, "\",", port, "\r\n");
    if (esp_expect_connect(15) != ESP_OK) {  /* DNS resolve + TCP connect; CONNECT then OK */
        connected = 0;
        esp_reset();
        return NET_NO_LINK;          /* DNS/connect failed or timed out */
    }

    /* Enter the raw byte pipe and stop exactly at the ">" prompt so the server's
     * first burst (connect NOTICEs) survives for the main loop instead of being
     * discarded. The CIPSEND reply is "OK\r\n>" — wait_prompt skips past the OK
     * and stops at '>', so neither the OK nor the prompt leak into the stream. */
    flush_rx();
    uart_tx_str("AT+CIPSEND\r\n");
    if (!wait_prompt(3)) {
        connected = 0;
        esp_reset();
        return NET_NO_LINK;          /* never got the data prompt */
    }
    connected = 1;
    return NET_OK;
}

void net_send(const char *s) {
    uart_tx_str(s);
}

void net_send_parts(const char *a, const char *b, const char *c, const char *d, const char *e) {
    uart_tx_parts(a, b, c, d, e);
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
/* The ESP path has no numeric backend status to report: its failures are
   already spelled out in the AT-command handling. */
u8 net_last_status(void) { return 0; }
void net_clear_stall(void) { uart_clear_stall(); }

u8 net_overrun(void) { return uart_overrun(); }
void net_clear_overrun(void) { uart_clear_overrun(); }

const char *net_last_detail(void) { return ""; }
