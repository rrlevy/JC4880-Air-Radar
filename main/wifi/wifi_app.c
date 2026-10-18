/* Wi-Fi pelo ESP32-C6 (esp_hosted, SDIO). Senha no namespace NVS "wifi".
   Nao ha senha no codigo-fonte. */
#include "wifi_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "wifi";

#define BIT_SCAN_DONE BIT0

static EventGroupHandle_t s_events;
static SemaphoreHandle_t s_lock;
static volatile bool s_hosted_ok;
static volatile bool s_connected;
static bool s_want_reconnect;
static int s_retries;
static char s_status[64] = "Iniciando Wi-Fi";
static char s_ssid[33];
static char s_pass[65];
static char s_boot_ssid[33];
static char s_boot_pass[65];
static wifi_ap_t s_aps[WIFI_AP_MAX];
static int s_ap_count;
static uint32_t s_scan_seq;
static wifi_ap_record_t s_scan_recs[WIFI_AP_MAX];
static wifi_ap_t s_fresh[WIFI_AP_MAX];

#define WIFI_KNOWN_MAX 8

typedef struct {
    char ssid[33];
    char pass[65];
} wifi_known_t;

static wifi_known_t s_known[WIFI_KNOWN_MAX];
static int s_known_n;
static volatile bool s_from_saved;
static volatile int s_scan_tries;
static esp_timer_handle_t s_scan_timer;
static uint32_t s_prompt_seq;
static char s_prompt_ssid[33];

/* ssid tem 32 bytes e password 64; um SSID cheio nao leva o zero final. */
static void copy_field(uint8_t *dst, size_t dst_n, const char *src)
{
    size_t n = src ? strlen(src) : 0;
    if (n > dst_n) n = dst_n;
    if (n) memcpy(dst, src, n);
}

static void set_status(const char *text)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_status, sizeof(s_status), "%s", text ? text : "");
    xSemaphoreGive(s_lock);
}

static void known_upsert_locked(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) return;
    for (int i = 0; i < s_known_n; i++) {
        if (strcmp(s_known[i].ssid, ssid) == 0) {
            snprintf(s_known[i].pass, sizeof(s_known[i].pass), "%s", pass ? pass : "");
            return;
        }
    }
    if (s_known_n >= WIFI_KNOWN_MAX) {
        memmove(&s_known[0], &s_known[1], (size_t)(WIFI_KNOWN_MAX - 1) * sizeof(wifi_known_t));
        s_known_n = WIFI_KNOWN_MAX - 1;
    }
    snprintf(s_known[s_known_n].ssid, sizeof(s_known[0].ssid), "%s", ssid);
    snprintf(s_known[s_known_n].pass, sizeof(s_known[0].pass), "%s", pass ? pass : "");
    s_known_n++;
}

static void known_forget_locked(const char *ssid)
{
    if (!ssid || !ssid[0]) return;
    for (int i = 0; i < s_known_n; i++) {
        if (strcmp(s_known[i].ssid, ssid) != 0) continue;
        memmove(&s_known[i], &s_known[i + 1], (size_t)(s_known_n - i - 1) * sizeof(wifi_known_t));
        s_known_n--;
        break;
    }
    if (strcmp(s_boot_ssid, ssid) == 0) {
        s_boot_ssid[0] = 0;
        s_boot_pass[0] = 0;
    }
}

/* Quem chama ja esta com s_lock. A rede de boot so muda quando a senha funciona. */
static void persist_locked(void)
{
    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_set_str(nvs, "ssid", s_boot_ssid);
    nvs_set_str(nvs, "pass", s_boot_pass);
    if (s_known_n > 0) {
        nvs_set_blob(nvs, "known", s_known, (size_t)s_known_n * sizeof(wifi_known_t));
    } else {
        nvs_erase_key(nvs, "known");
    }
    nvs_commit(nvs);
    nvs_close(nvs);
}

static void load_store(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READWRITE, &nvs) != ESP_OK) {
        xSemaphoreGive(s_lock);
        return;
    }
    size_t n = sizeof(s_known);
    esp_err_t err = nvs_get_blob(nvs, "known", s_known, &n);
    if (err == ESP_OK && n >= sizeof(wifi_known_t) && (n % sizeof(wifi_known_t)) == 0) {
        s_known_n = (int)(n / sizeof(wifi_known_t));
        if (s_known_n > WIFI_KNOWN_MAX) s_known_n = WIFI_KNOWN_MAX;
    } else {
        s_known_n = 0;
    }

    char ssid[33] = {0};
    char pass[65] = {0};
    size_t ns = sizeof(ssid);
    bool have = nvs_get_str(nvs, "ssid", ssid, &ns) == ESP_OK && ssid[0];
    if (have) {
        ns = sizeof(pass);
        if (nvs_get_str(nvs, "pass", pass, &ns) != ESP_OK) pass[0] = 0;
        snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
        snprintf(s_pass, sizeof(s_pass), "%s", pass);
        snprintf(s_boot_ssid, sizeof(s_boot_ssid), "%s", ssid);
        snprintf(s_boot_pass, sizeof(s_boot_pass), "%s", pass);
        bool existed = false;
        for (int i = 0; i < s_known_n; i++) {
            if (strcmp(s_known[i].ssid, ssid) == 0) {
                existed = true;
                break;
            }
        }
        known_upsert_locked(ssid, pass);
        if (!existed) {
            nvs_set_blob(nvs, "known", s_known, (size_t)s_known_n * sizeof(wifi_known_t));
            nvs_commit(nvs);
            ESP_LOGI(TAG, "senha da rede atual migrada para a lista");
        }
    }
    nvs_close(nvs);
    ESP_LOGI(TAG, "redes com senha salva: %d", s_known_n);
    xSemaphoreGive(s_lock);
}

static bool auth_rejected(int reason)
{
    return reason == WIFI_REASON_MIC_FAILURE ||
           reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
           reason == WIFI_REASON_AUTH_FAIL ||
           reason == WIFI_REASON_HANDSHAKE_TIMEOUT;
}

static int cmp_rssi(const void *a, const void *b)
{
    const wifi_ap_t *pa = a;
    const wifi_ap_t *pb = b;
    return pb->rssi - pa->rssi;
}

static void start_scan(bool from_user);

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_ssid[0]) {
            s_want_reconnect = true;
            esp_wifi_connect();
            set_status("Conectando");
        } else {
            set_status("Escolha uma rede");
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        wifi_event_sta_disconnected_t *ev = data;
        int reason = ev ? ev->reason : 0;
        ESP_LOGW(TAG, "desconectou, motivo %d", reason);
        if (auth_rejected(reason)) {
            s_want_reconnect = false;
            s_retries = 0;
            if (s_from_saved) {
                s_from_saved = false;
                xSemaphoreTake(s_lock, portMAX_DELAY);
                snprintf(s_prompt_ssid, sizeof(s_prompt_ssid), "%s", s_ssid);
                known_forget_locked(s_prompt_ssid);
                persist_locked();
                s_prompt_seq++;
                xSemaphoreGive(s_lock);
                ESP_LOGW(TAG, "senha salva descartada");
            }
            set_status("Senha recusada");
        } else if (s_want_reconnect && s_ssid[0] && s_retries < 4) {
            s_retries++;
            esp_wifi_connect();
            set_status("Tentando de novo");
        } else {
            s_want_reconnect = false;
            if (reason == WIFI_REASON_NO_AP_FOUND) set_status("Rede nao encontrada");
            else set_status("Sem conexao");
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        wifi_event_sta_scan_done_t *done = data;
        uint16_t n = WIFI_AP_MAX;
        /* Sempre libera a lista interna, mesmo quando a busca falha. */
        if (esp_wifi_scan_get_ap_records(&n, s_scan_recs) != ESP_OK) n = 0;
        bool failed = done && done->status != 0;
        if (failed) n = 0;

        int fresh_n = 0;
        for (int i = 0; i < (int)n && fresh_n < WIFI_AP_MAX; i++) {
            if (s_scan_recs[i].ssid[0] == 0) continue;
            bool dup = false;
            for (int k = 0; k < fresh_n; k++) {
                if (strcmp(s_fresh[k].ssid, (char *)s_scan_recs[i].ssid) == 0) {
                    if (s_scan_recs[i].rssi > s_fresh[k].rssi) s_fresh[k].rssi = s_scan_recs[i].rssi;
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            wifi_ap_t *ap = &s_fresh[fresh_n++];
            snprintf(ap->ssid, sizeof(ap->ssid), "%s", (char *)s_scan_recs[i].ssid);
            ap->rssi = s_scan_recs[i].rssi;
            ap->open = s_scan_recs[i].authmode == WIFI_AUTH_OPEN ||
                       s_scan_recs[i].authmode == WIFI_AUTH_OWE;
        }

        if (fresh_n == 0 && s_scan_tries > 0) {
            s_scan_tries--;
            ESP_LOGW(TAG, "scan vazio, nova tentativa");
            if (s_scan_timer) esp_timer_start_once(s_scan_timer, 800000);
            set_status("Buscando redes");
            xEventGroupSetBits(s_events, BIT_SCAN_DONE);
            return;
        }

        if (fresh_n > 1) qsort(s_fresh, (size_t)fresh_n, sizeof(s_fresh[0]), cmp_rssi);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (fresh_n > 0) {
            memcpy(s_aps, s_fresh, (size_t)fresh_n * sizeof(wifi_ap_t));
            s_ap_count = fresh_n;
        }
        s_scan_tries = 0;
        s_scan_seq++;
        int shown = s_ap_count;
        xSemaphoreGive(s_lock);
        if (s_connected) set_status("Conectado");
        else set_status(shown ? "Toque numa rede" : "Nenhuma rede");
        ESP_LOGI(TAG, "scan: %d redes", shown);
        xEventGroupSetBits(s_events, BIT_SCAN_DONE);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        s_retries = 0;
        s_from_saved = false;
        s_want_reconnect = true;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        snprintf(s_boot_ssid, sizeof(s_boot_ssid), "%s", s_ssid);
        snprintf(s_boot_pass, sizeof(s_boot_pass), "%s", s_pass);
        known_upsert_locked(s_ssid, s_pass);
        persist_locked();
        xSemaphoreGive(s_lock);
        set_status("Conectado");
        ESP_LOGI(TAG, "conectado em %s", s_ssid);
    }
}

static void wifi_task(void *arg)
{
    (void)arg;
    load_store();

    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
        set_status("Modulo C6 sem resposta");
        s_hosted_ok = false;
        vTaskDelete(NULL);
        return;
    }
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    if (s_ssid[0]) {
        wifi_config_t wc = {0};
        copy_field(wc.sta.ssid, sizeof(wc.sta.ssid), s_ssid);
        copy_field(wc.sta.password, sizeof(wc.sta.password), s_pass);
        wc.sta.threshold.authmode = s_pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        wc.sta.pmf_cfg.capable = true;
        esp_wifi_set_config(WIFI_IF_STA, &wc);
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    s_hosted_ok = true;
    vTaskDelete(NULL);
}

static void scan_retry_cb(void *arg)
{
    (void)arg;
    start_scan(false);
}

void wifi_app_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    const esp_timer_create_args_t args = {
        .callback = scan_retry_cb,
        .name = "wifi_scan",
    };
    esp_timer_create(&args, &s_scan_timer);
    xTaskCreate(wifi_task, "wifi", 8192, NULL, 5, NULL);
}

bool wifi_is_connected(void) { return s_connected; }
bool wifi_hosted_ok(void) { return s_hosted_ok; }

void wifi_status(char *dst, int dst_n)
{
    if (!dst || dst_n <= 0) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(dst, dst_n, "%s", s_status);
    xSemaphoreGive(s_lock);
}

void wifi_ssid(char *dst, int dst_n)
{
    if (!dst || dst_n <= 0) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(dst, dst_n, "%s", s_ssid);
    xSemaphoreGive(s_lock);
}

int wifi_rssi(void)
{
    static int64_t last_us;
    static int cached = -127;
    if (!s_connected) return -127;
    int64_t now = esp_timer_get_time();
    if (now - last_us < 500000 && cached != -127) return cached;
    last_us = now;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) cached = ap.rssi;
    return cached;
}

bool wifi_has_saved(const char *ssid)
{
    if (!ssid || !ssid[0]) return false;
    bool found = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < s_known_n; i++) {
        if (strcmp(s_known[i].ssid, ssid) == 0) {
            found = true;
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return found;
}

uint32_t wifi_prompt_seq(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t v = s_prompt_seq;
    xSemaphoreGive(s_lock);
    return v;
}

void wifi_prompt_ssid(char *dst, int dst_n)
{
    if (!dst || dst_n <= 0) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(dst, dst_n, "%s", s_prompt_ssid);
    xSemaphoreGive(s_lock);
}

static void start_scan(bool from_user)
{
    if (!s_hosted_ok) return;
    if (from_user) {
        s_scan_tries = 3;
        if (s_scan_timer) esp_timer_stop(s_scan_timer);
    }
    wifi_scan_config_t scan = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    esp_err_t err = esp_wifi_scan_start(&scan, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan: %s", esp_err_to_name(err));
        if (s_scan_tries > 0) {
            s_scan_tries--;
            if (s_scan_timer) esp_timer_start_once(s_scan_timer, 800000);
            set_status("Buscando redes");
        } else {
            set_status("Falha ao buscar redes");
        }
        return;
    }
    set_status("Buscando redes");
}

void wifi_request_scan(void)
{
    start_scan(true);
}

uint32_t wifi_scan_seq(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t v = s_scan_seq;
    xSemaphoreGive(s_lock);
    return v;
}

int wifi_copy_aps(wifi_ap_t *out, int max)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = s_ap_count;
    if (n > max) n = max;
    if (out && n) memcpy(out, s_aps, (size_t)n * sizeof(wifi_ap_t));
    xSemaphoreGive(s_lock);
    return n;
}

static void connect_now(const char *ssid, const char *password, bool from_saved)
{
    if (!s_hosted_ok || !ssid || !ssid[0]) return;
    s_from_saved = from_saved;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
    snprintf(s_pass, sizeof(s_pass), "%s", password ? password : "");
    xSemaphoreGive(s_lock);

    wifi_config_t wc = {0};
    copy_field(wc.sta.ssid, sizeof(wc.sta.ssid), ssid);
    copy_field(wc.sta.password, sizeof(wc.sta.password), password);
    wc.sta.threshold.authmode = (password && password[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    s_retries = 0;
    s_want_reconnect = true;
    s_connected = false;
    set_status(from_saved ? "Conectando com senha salva" : "Conectando");
    esp_wifi_disconnect();
    esp_wifi_connect();
}

void wifi_connect_to(const char *ssid, const char *password)
{
    connect_now(ssid, password, false);
}

bool wifi_connect_saved(const char *ssid)
{
    if (!ssid || !ssid[0]) return false;
    char pass[65];
    bool found = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < s_known_n; i++) {
        if (strcmp(s_known[i].ssid, ssid) == 0) {
            snprintf(pass, sizeof(pass), "%s", s_known[i].pass);
            found = true;
            break;
        }
    }
    xSemaphoreGive(s_lock);
    if (!found) return false;
    connect_now(ssid, pass, true);
    return true;
}
