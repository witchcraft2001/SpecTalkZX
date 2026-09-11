/*
 * netdll.c — NETDLL.EXE: UNET DLL loader diagnostic (port plan Stage 1).
 *
 * Built on the win0 layout (WIN0+WIN1+WIN2 -- see the port plan's "переход
 * на трёхоконную раскладку"), the same layout SPTALK.EXE itself now uses:
 * this is the isolated testbed that proved the DLL window-swap mechanics
 * (unetcore.s's _wrt_p1 fix so the win0 RST trampolines don't evict the DLL
 * mid-call, _HIGH placement, the P0:0x0100 app-dir path replacing APPINFO)
 * before SPTALK.EXE adopted the same layout. See tools/test_netdll_win0.js
 * for the automated harness verification (SELECT/LOAD/GETCAPS/ABI end to end
 * against a real UNETxxxx.DLL).
 *
 * With a `NETDLL <host> <port>` command line it also runs stage S5: the exact
 * sequence SPTALK performs on /server -- CONNECT, a non-blocking RECV, then the
 * first SEND -- and prints the backend's own LASTERR line after each step. That
 * makes it the bisection tool for a link that connects but cannot send: same
 * win0 layout, same DLL-in-WIN1 swap, same call wrappers as SPTALK, but no UI,
 * no scrollback pages in WIN3 and no 50 Hz redraw. If S5 fails the way SPTALK
 * does, the fault is in the window/DLL mechanics; if S5 succeeds, it is in what
 * SPTALK does around the calls.
 *
 * Stage codes and the terminal RESULT line follow the convention documented
 * in sprinter-3C509B/AGENTS.md ("stable stage codes ... end with an
 * unambiguous RESULT OK or RESULT FAIL") so a host-side harness can grep for
 * them. RESULT reflects the loader itself (SELECT/LOAD/name/ABI); NETSTART and
 * GETINFO are best-effort and reported but do not flip it — this program does
 * not require NETUP/IFUP to have been run.
 */
#include <sprinter.h>
#include "unetld.h"

static void line(const char *s) { dss_puts(s); dss_puts("\r\n"); }

static void kv(const char *label, const char *value)
{
    dss_puts(label);
    dss_puts(value && value[0] ? value : "(empty)");
    dss_puts("\r\n");
}

/* Avoid u8 division/modulo: sprinter.lib doesn't carry SDCC's __divuchar
 * runtime helper, so a plain '/'/'%' on a u8 fails to link. Values here are
 * always < 100 (NERR_* codes). */
static void put_dec2(char **o, u8 v)
{
    u8 tens = 0;
    while (v >= 10) { v -= 10; tens++; }
    *(*o)++ = (char)('0' + tens);
    *(*o)++ = (char)('0' + v);
}

static void kdec(const char *label, u16 v)
{
    char buf[8], *o = buf;
    u16 scale = 10000;
    u8 leading = 1;
    while (scale) {
        u8 d = 0;
        while (v >= scale) { v -= scale; d++; }
        if (d || !leading || scale == 1) { *o++ = (char)('0' + d); leading = 0; }
        scale = scale == 10000 ? 1000 : scale == 1000 ? 100 : scale == 100 ? 10 : scale == 10 ? 1 : 0;
    }
    *o = 0;
    kv(label, buf);
}

/* The DLL's own account of the last failure: which stage, its internal codes,
 * and -- on UNETRTL -- the NIC transmit diagnostics captured at that moment
 * (stage/ISR/TSR/CR). TSR is what separates "the peer never answered" from
 * "the card never put the frame on the wire". */
static void lasterr(const char *label)
{
    char buf[96];
    buf[0] = 0;
    unet_lasterr(buf, sizeof(buf));
    kv(label, buf);
}

static void khex(const char *label, u16 v)
{
    static const char digits[] = "0123456789ABCDEF";
    char buf[7];
    buf[0] = '0'; buf[1] = 'x';
    buf[2] = digits[(v >> 12) & 0xF];
    buf[3] = digits[(v >> 8) & 0xF];
    buf[4] = digits[(v >> 4) & 0xF];
    buf[5] = digits[v & 0xF];
    buf[6] = 0;
    kv(label, buf);
}

void main(void)
{
    i8 rc;
    u16 bits;
    char buf[128];

    dss_clrscr();
    line("=== NETDLL -- UNET loader diagnostic ===");
    line("");

    line("[S1] SELECT (read NET env var)");
    rc = unetld_select();
    if (rc != 0) {
        kv("     NET tag    : ", unetld_net_tag());
        kv("     error      : ", unetld_error() == UNETLD_E_NOENV
                                    ? "E_NOENV (NET not set -- run NETUP/IFUP first)"
                                    : "E_BADVALUE (NET must be 3-4 chars [A-Z0-9])");
        line("");
        line("RESULT FAIL");
        dss_waitkey();
        dss_exit(1);
    }
    kv("     NET tag    : ", unetld_net_tag());
    kv("     DLL name   : ", unetld_dll_name());

    line("[S2] LOAD (open, decode, relocate, INIT, GETCAPS/ABI)");
    rc = unetld_load();
    if (rc != 0) {
        const char *why = "?";
        switch (unetld_error()) {
            case UNETLD_E_LOAD: why = "E_LOAD (open/decode/INIT failed -- DLL missing or bad)"; break;
            case UNETLD_E_NAME: why = "E_NAME (self-reported name != expected tag)"; break;
            case UNETLD_E_CALL: why = "E_CALL (GETCAPS dispatch/status failed)"; break;
            case UNETLD_E_ABI:  why = "E_ABI (unsupported ABI major or no CAP_TCP)"; break;
        }
        line(why);
        kv("     path tried : ", unetld_dll_path());   /* "" = fell back to the bare name */
        unetld_unload();
        line("");
        line("RESULT FAIL");
        dss_waitkey();
        dss_exit(1);
    }
    kv("     self-name  : ", unet_dll_name());
    khex("     caps       : ", unet_caps());
    khex("     abi        : ", unet_abi());
    line("     LOAD OK");

    line("");
    line("[S3] NETSTART (STATUS(0xFF) + NETINIT -- needs NETUP/IFUP already run)");
    rc = unetld_netstart();
    if (rc != 0) {
        char msg[48], *o = msg;
        const char *p = "     NETSTART: not brought up (status=";
        while (*p) *o++ = *p++;
        put_dec2(&o, unetld_last_status());
        *o++ = ')';
        *o = 0;
        line(msg);
    } else {
        line("     NETSTART: OK, link is up");

        line("");
        line("[S4] GETINFO");
        if (unet_getinfo(UNET_IF_BACKEND, buf, sizeof(buf)) == NERR_OK) kv("     backend    : ", buf);
        if (unet_getinfo(UNET_IF_IP, buf, sizeof(buf)) == NERR_OK)      kv("     ip         : ", buf);
        if (unet_getinfo(UNET_IF_MAC, buf, sizeof(buf)) == NERR_OK)     kv("     mac        : ", buf);
        if (unet_getinfo(UNET_IF_HW, buf, sizeof(buf)) == NERR_OK)      kv("     hw         : ", buf);

        /* [S5] live link, only with a command line. */
        {
            /* The command line lives at P0:0x0080 -- WIN0. Never hand a WIN0
             * pointer to the DLL: UNETRTL's cold overlay maps its OWN page over
             * WIN0 for the ARP/DNS frame builders (lib/win0cold.asm), so the
             * string vanishes mid-call and the dotted quad silently fails to
             * parse, turning a literal IP into a DNS lookup. Stage both
             * arguments into _DATA (WIN2) first, exactly as net_unet.s does. */
            static char host[80], port[16];
            char *cl = dss_cmdline(), *o;
            u8 i;
            while (*cl == ' ') cl++;
            o = host; i = 0;
            while (*cl && *cl != ' ' && i < sizeof(host) - 1) { *o++ = *cl++; i++; }
            *o = 0;
            while (*cl == ' ') cl++;
            o = port; i = 0;
            while (*cl && *cl != ' ' && i < sizeof(port) - 1) { *o++ = *cl++; i++; }
            *o = 0;
            if (!port[0]) { port[0] = '6'; port[1] = '6'; port[2] = '6'; port[3] = '7'; port[4] = 0; }
            if (host[0]) {
                line("");
                line("[S5] LIVE (CONNECT, RECV, SEND -- the /server sequence)");
                kv("     host       : ", host);
                kv("     port       : ", port);
                rc = (i8)unet_connect(host, port);
                kdec("     connect    : NERR ", (u16)(u8)rc);
                lasterr("     lasterr    : ");
                if (rc == NERR_OK) {
                    u16 flags = 0;
                    i16 n;
                    /* SPTALK drains before every send; do the same, so the DLL
                     * sees the identical call order. */
                    n = unet_recv(buf, 96, 0, &flags);
                    kdec("     recv(0ms)  : ", (u16)(n < 0 ? 0 : n));
                    n = unet_send("NICK netdll\r\n", 13);
                    if (n < 0) {
                        kdec("     send       : FAILED, NERR ", (u16)(-n));
                    } else {
                        kdec("     send       : bytes ", (u16)n);
                    }
                    lasterr("     lasterr    : ");
                    flags = 0;
                    n = unet_recv(buf, 96, 3000, &flags);
                    kdec("     recv(3s)   : ", (u16)(n < 0 ? 0 : n));
                    if (n < 0) kdec("     recv NERR  : ", (u16)(-n));
                    lasterr("     lasterr    : ");
                    unet_close();
                }
            } else {
                line("");
                line("     (pass `NETDLL <host> [port]` to also run the live S5 stage)");
            }
        }
    }
    (void)bits;

    unetld_unload();

    line("");
    line("RESULT OK");
    dss_waitkey();
    dss_exit(0);
}
