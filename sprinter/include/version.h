/*
 * version.h — application name / version / network backend tag.
 *
 * APP_BACKEND distinguishes the network card the binary was built for:
 *   "ESP" — SprinterWiFi / ESP card over the TL16C550 UART (current default)
 *   "RTL" — RTL8019 (NE2000) ISA card (future build)
 * Override at build time with -DAPP_BACKEND='"RTL"'.
 */
#ifndef VERSION_H
#define VERSION_H

#define APP_NAME    "SprinTalk"
#define APP_VER     "0.1"
#define APP_AUTHOR  "Dmitry Mikhalchenkov, SprinterTeam. FidoNet:2:5030/1997.10"

/* Compiler-stamped build date/time. Only fresh in the translation unit being
 * compiled, so the Makefile force-rebuilds main.c (which prints it) every build. */
#define APP_BUILD   __DATE__ " " __TIME__

#ifndef APP_BACKEND
#define APP_BACKEND "ESP"
#endif

#define APP_TITLE   APP_NAME " " APP_VER " " APP_BACKEND   /* e.g. "SprinTalk 0.1 ESP" */

#endif /* VERSION_H */
