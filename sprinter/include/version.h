/*
 * version.h — application name / version / network backend tag.
 *
 * APP_BACKEND distinguishes the transport the binary was built against:
 *   "ESP"  — SprinterWiFi / ESP card over the TL16C550 UART (NET_BACKEND=esp)
 *   "UNET" — any UNETxxxx.DLL, card picked at runtime (NET_BACKEND=unet)
 * It must track NET_BACKEND: it is printed in the banner, in CTCP VERSION and
 * in the USER realname, so a stale "ESP" here sends users hunting for a Wi-Fi
 * card that this build never touches. Override with -DAPP_BACKEND='"RTL"'.
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
#ifdef NET_BACKEND_UNET
#define APP_BACKEND "UNET"
#else
#define APP_BACKEND "ESP"
#endif
#endif

#define APP_TITLE   APP_NAME " " APP_VER " " APP_BACKEND   /* e.g. "SprinTalk 0.1 ESP" */

#endif /* VERSION_H */
