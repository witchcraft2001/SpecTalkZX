/*
 * netcfg.h — load/parse NET.CFG (the SprinterWiFi network kit config).
 *
 * Location: the directory named by %NET_DIR% (e.g. C:\NET), file NET.CFG.
 * If %NET_DIR% is unset or the file is missing there, falls back to NET.CFG in
 * the current directory. INI-style "KEY=VALUE" lines; '#'/';' comments ignored.
 */
#ifndef NETCFG_H
#define NETCFG_H

#include <sprinter.h>

typedef struct {
    char net_dir[64];     /* value of %NET_DIR% ("" if unset) */
    char path[80];        /* path actually opened */
    u8   loaded;          /* 1 if NET.CFG was read and parsed */

    char ssid[34];
    char pass[34];
    char dhcp[4];         /* "1"/"0" */
    char ip[18];
    char gateway[18];
    char netmask[18];
    char dns1[18];
    char dns2[18];
    char ntp[34];
    char baud[8];
} netcfg_t;

/* Returns 0 on success, <0 on error (-1 open failed, -2 read failed). */
i8 netcfg_load(netcfg_t *cfg);

#endif /* NETCFG_H */
