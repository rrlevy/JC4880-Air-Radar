#pragma once

#include <stdbool.h>
#include <stdint.h>

#define WIFI_AP_MAX 16

typedef struct {
    char ssid[33];
    int rssi;
    bool open;
} wifi_ap_t;

void wifi_app_start(void);
bool wifi_is_connected(void);
bool wifi_hosted_ok(void);
void wifi_status(char *dst, int dst_n);
void wifi_ssid(char *dst, int dst_n);
/* RSSI da rede atual em dBm, ou -127 se desconectado. */
int wifi_rssi(void);
/* Ha senha guardada para esse SSID (rede aberta conta como senha vazia). */
bool wifi_has_saved(const char *ssid);
void wifi_request_scan(void);
uint32_t wifi_scan_seq(void);
int wifi_copy_aps(wifi_ap_t *out, int max);
void wifi_connect_to(const char *ssid, const char *password);
/* Usa a senha guardada. Devolve false se essa rede ainda nao tem senha. */
bool wifi_connect_saved(const char *ssid);
/* Sobe quando uma senha guardada e recusada e precisa ser digitada de novo. */
uint32_t wifi_prompt_seq(void);
void wifi_prompt_ssid(char *dst, int dst_n);
