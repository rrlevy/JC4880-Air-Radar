/* Leitura e gravacao da configuracao. Quem muda um campo grava o conjunto.
   Nao chame o modelo de avioes segurando o mutex daqui. */
#include "cfg.h"

#include "airports.h"
#include "model.h"
#include "net.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "cfg";

/* Aeroporto de Congonhas (CGH). Graus * 1e7, o mesmo ponto de airports.c. */
#define LAT_E7_DEFAULT ((int32_t)(-236276570))
#define LON_E7_DEFAULT ((int32_t)(-466546010))

/* Do mais perto ao mais longe. O indice 4 (15 NM) e o padrao.
   250 NM e o teto: a API de ADS-B recusa distancia maior. */
static const float s_steps[] = {1.f, 2.f, 5.f, 10.f, 15.f, 30.f, 50.f, 100.f, 250.f};
#define RANGE_DEFAULT 4

static SemaphoreHandle_t s_lock;
static int32_t s_lat_e7 = LAT_E7_DEFAULT;
static int32_t s_lon_e7 = LON_E7_DEFAULT;
static int s_range_idx = RANGE_DEFAULT;
static int s_period = 5;
static int s_heading;
static uint8_t s_filters = CFG_FILT_ALL;
static uint8_t s_kinds = CFG_KIND_DEFAULT;
static uint32_t s_kind_gen;
static uint32_t s_gen;
static uint32_t s_range_gen;
/* Sem a chave "thm", ou com valor invalido, o tema e o Generico. */
static int s_theme = CFG_THEME_GENERIC;
static bool s_show_map = true;
static bool s_show_rivers = true;
static char s_title[CFG_TITLE_MAX + 1] = "Radar";

static void title_normalize(const char *src, char *dst, int n)
{
    int i = 0;
    if (n > CFG_TITLE_MAX + 1) n = CFG_TITLE_MAX + 1;
    if (n < 2) {
        if (n > 0) dst[0] = 0;
        return;
    }
    if (!src) src = "";
    while (*src == ' ') src++;
    for (; *src && i < n - 1; src++) {
        unsigned char c = (unsigned char)*src;
        if (c < 32 || c > 126) continue;
        dst[i++] = (char)c;
    }
    while (i > 0 && dst[i - 1] == ' ') i--;
    dst[i] = 0;
    if (!dst[0]) snprintf(dst, (size_t)n, "Radar");
}

static int32_t deg_e7(double d)
{
    double v = d * 10000000.0;
    if (v >= 0) return (int32_t)(v + 0.5);
    return (int32_t)(v - 0.5);
}

static double e7_deg(int32_t v)
{
    return (double)v / 10000000.0;
}

static void persist(void)
{
    int32_t lat, lon;
    uint8_t rng, flt, per, thm, map, rio, knd;
    uint16_t hdg;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    lat = s_lat_e7;
    lon = s_lon_e7;
    rng = (uint8_t)s_range_idx;
    flt = s_filters;
    knd = s_kinds;
    per = (uint8_t)s_period;
    hdg = (uint16_t)s_heading;
    thm = (uint8_t)s_theme;
    map = s_show_map ? 1 : 0;
    rio = s_show_rivers ? 1 : 0;
    xSemaphoreGive(s_lock);
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "lat", lat);
    nvs_set_i32(h, "lon", lon);
    nvs_set_u8(h, "rng", rng);
    nvs_set_u8(h, "flt", flt);
    nvs_set_u8(h, "knd", knd);
    nvs_set_u8(h, "per", per);
    nvs_set_u16(h, "hdg", hdg);
    nvs_set_u8(h, "thm", thm);
    nvs_set_u8(h, "map", map);
    nvs_set_u8(h, "rio", rio);
    nvs_commit(h);
    nvs_close(h);
}

void cfg_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "centro padrao %.6f %.6f", e7_deg(s_lat_e7), e7_deg(s_lon_e7));
        return;
    }
    int32_t lat = s_lat_e7, lon = s_lon_e7;
    uint8_t rng = (uint8_t)s_range_idx, flt = s_filters, per = (uint8_t)s_period;
    uint8_t knd_raw = CFG_KIND_DEFAULT;
    uint8_t thm = (uint8_t)s_theme;
    uint8_t map = 1, rio = 1;
    uint16_t hdg = 0;
    nvs_get_i32(h, "lat", &lat);
    nvs_get_i32(h, "lon", &lon);
    nvs_get_u8(h, "rng", &rng);
    nvs_get_u8(h, "flt", &flt);
    bool knd_found = nvs_get_u8(h, "knd", &knd_raw) == ESP_OK;
    nvs_get_u8(h, "per", &per);
    nvs_get_u16(h, "hdg", &hdg);
    nvs_get_u8(h, "thm", &thm);
    nvs_get_u8(h, "map", &map);
    nvs_get_u8(h, "rio", &rio);
    char title[CFG_TITLE_MAX + 1];
    size_t title_n = sizeof(title);
    if (nvs_get_str(h, "ttl", title, &title_n) == ESP_OK) title_normalize(title, s_title, sizeof(s_title));
    /* Uma vez: o centro que ja estava na placa passa para Congonhas. Depois, a escolha do usuario fica. */
    uint8_t ctr = 0;
    bool center_up = nvs_get_u8(h, "ctr", &ctr) != ESP_OK || ctr != 1;
    nvs_close(h);
    if (center_up) {
        lat = LAT_E7_DEFAULT;
        lon = LON_E7_DEFAULT;
    }
    if (lat < -900000000 || lat > 900000000) lat = LAT_E7_DEFAULT;
    if (lon < -1800000000 || lon > 1800000000) lon = LON_E7_DEFAULT;
    /* O indice 9 era 500 NM. Passa para 250, o novo teto. */
    bool range_up = false;
    int nsteps = (int)(sizeof(s_steps) / sizeof(s_steps[0]));
    if (rng == 9) {
        rng = (uint8_t)(nsteps - 1);
        range_up = true;
    } else if (rng >= nsteps) {
        rng = RANGE_DEFAULT;
    }
    if (per < 2 || per > 120) per = 5;
    if (hdg > 359) hdg = 0;
    if (thm >= CFG_THEME_COUNT) thm = CFG_THEME_GENERIC;
    /* 0x10 era o grupo Abra. Uma mascara vazia depois do corte volta para todos. */
    bool filt_up = false;
    uint8_t masked = (uint8_t)(flt & CFG_FILT_ALL);
    if (masked == 0) masked = CFG_FILT_ALL;
    if (masked != flt) {
        flt = masked;
        filt_up = true;
    }
    /* Sem a chave, aviao ligado e helicoptero desligado. Bit estranho sai fora. */
    bool knd_up = false;
    uint8_t knd = CFG_KIND_DEFAULT;
    if (knd_found) {
        knd = (uint8_t)(knd_raw & (CFG_KIND_PLANE | CFG_KIND_HELI));
        if (knd != knd_raw) knd_up = true;
    }
    s_lat_e7 = lat;
    s_lon_e7 = lon;
    s_range_idx = rng;
    s_filters = flt;
    s_kinds = knd;
    s_period = per;
    s_heading = hdg;
    s_theme = thm;
    s_show_map = map != 0;
    s_show_rivers = rio != 0;
    if (filt_up || knd_up || range_up || center_up) persist();
    if (center_up) {
        nvs_handle_t w;
        if (nvs_open("cfg", NVS_READWRITE, &w) == ESP_OK) {
            nvs_set_u8(w, "ctr", 1);
            nvs_commit(w);
            nvs_close(w);
        }
        ESP_LOGI(TAG, "centro gravado em Congonhas");
    }
    ESP_LOGI(TAG, "centro %.6f %.6f  alcance %.0f NM  periodo %ds  proa %03d  filtros 0x%02x  tipos 0x%02x  tema %d  mapa %d  rios %d  titulo %s",
             e7_deg(lat), e7_deg(lon), s_steps[rng], per, hdg, flt, knd, thm,
             s_show_map ? 1 : 0, s_show_rivers ? 1 : 0, s_title);
}

double cfg_lat(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    double v = e7_deg(s_lat_e7);
    xSemaphoreGive(s_lock);
    return v;
}

double cfg_lon(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    double v = e7_deg(s_lon_e7);
    xSemaphoreGive(s_lock);
    return v;
}

float cfg_range_nm(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    float v = s_steps[s_range_idx];
    xSemaphoreGive(s_lock);
    return v;
}

int cfg_range_index(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int v = s_range_idx;
    xSemaphoreGive(s_lock);
    return v;
}

int cfg_period_s(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int v = s_period;
    xSemaphoreGive(s_lock);
    return v;
}

uint8_t cfg_filters(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t v = s_filters;
    xSemaphoreGive(s_lock);
    return v;
}

uint32_t cfg_gen(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t v = s_gen;
    xSemaphoreGive(s_lock);
    return v;
}

float cfg_range_step(int index)
{
    if (index < 0) index = 0;
    int n = (int)(sizeof(s_steps) / sizeof(s_steps[0]));
    if (index >= n) index = n - 1;
    return s_steps[index];
}

int cfg_range_step_count(void)
{
    return (int)(sizeof(s_steps) / sizeof(s_steps[0]));
}

static void set_center_e7(int32_t lat, int32_t lon)
{
    bool changed;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int32_t dlat = lat - s_lat_e7;
    int32_t dlon = lon - s_lon_e7;
    if (dlat < 0) dlat = -dlat;
    if (dlon < 0) dlon = -dlon;
    changed = dlat > 20 || dlon > 20;
    s_lat_e7 = lat;
    s_lon_e7 = lon;
    if (changed) s_gen++;
    xSemaphoreGive(s_lock);
    persist();
    if (changed) {
        ESP_LOGI(TAG, "centro %.6f %.6f", e7_deg(lat), e7_deg(lon));
        model_clear();
    }
}

void cfg_set_center(double lat, double lon)
{
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return;
    set_center_e7(deg_e7(lat), deg_e7(lon));
}

void cfg_reset_center(void)
{
    set_center_e7(LAT_E7_DEFAULT, LON_E7_DEFAULT);
}

bool cfg_set_iata(const char *iata)
{
    const apt_t *a = airport_by_iata(iata);
    if (!a) return false;
    set_center_e7(deg_e7(a->lat), deg_e7(a->lon));
    return true;
}

int cfg_heading_deg(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int v = s_heading;
    xSemaphoreGive(s_lock);
    return v;
}

void cfg_set_heading_deg(int degrees)
{
    degrees %= 360;
    if (degrees < 0) degrees += 360;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = degrees != s_heading;
    s_heading = degrees;
    xSemaphoreGive(s_lock);
    if (!changed) return;
    persist();
    ESP_LOGI(TAG, "proa %03d", degrees);
}

void cfg_title(char *dst, int n)
{
    if (!dst || n <= 0) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(dst, (size_t)n, "%s", s_title);
    xSemaphoreGive(s_lock);
}

void cfg_set_title(const char *text)
{
    char next[CFG_TITLE_MAX + 1];
    title_normalize(text, next, sizeof(next));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = strcmp(s_title, next) != 0;
    if (changed) memcpy(s_title, next, sizeof(s_title));
    xSemaphoreGive(s_lock);
    if (!changed) return;
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "ttl", next);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "titulo %s", next);
}

void cfg_set_period_s(int seconds)
{
    if (seconds < 2) seconds = 2;
    if (seconds > 120) seconds = 120;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = seconds != s_period;
    s_period = seconds;
    xSemaphoreGive(s_lock);
    if (!changed) return;
    persist();
    ESP_LOGI(TAG, "periodo %ds", seconds);
}

void cfg_set_filters(uint8_t bits)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = bits != s_filters;
    s_filters = bits;
    xSemaphoreGive(s_lock);
    if (!changed) return;
    persist();
    ESP_LOGI(TAG, "filtros 0x%02x", bits);
    model_refilter();
}

uint8_t cfg_kinds(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t v = s_kinds;
    xSemaphoreGive(s_lock);
    return v;
}

uint32_t cfg_kind_gen(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t v = s_kind_gen;
    xSemaphoreGive(s_lock);
    return v;
}

void cfg_set_kinds(uint8_t bits)
{
    bits = (uint8_t)(bits & (CFG_KIND_PLANE | CFG_KIND_HELI));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = bits != s_kinds;
    uint8_t added = (uint8_t)(bits & (uint8_t)~s_kinds);
    s_kinds = bits;
    if (added) s_kind_gen++;
    xSemaphoreGive(s_lock);
    if (!changed) return;
    persist();
    ESP_LOGI(TAG, "tipos 0x%02x", bits);
    model_refilter();
    /* Ligar um tipo pede o que a consulta anterior deixou de fora. */
    if (added) net_request_rush();
}

void cfg_set_range_index(int index)
{
    int n = cfg_range_step_count();
    if (index < 0) index = 0;
    if (index >= n) index = n - 1;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int old = s_range_idx;
    bool changed = index != old;
    s_range_idx = index;
    if (changed && index > old) s_range_gen++;
    xSemaphoreGive(s_lock);
    if (!changed) return;
    persist();
    ESP_LOGI(TAG, "alcance %.0f NM", s_steps[index]);
    model_refilter();
    /* Mais longe: o que esta na tela nao cobre o raio novo. Busca agora.
       Mais perto: a lista atual ja tem esses avioes; o refilter corta o resto. */
    if (index > old) net_request_rush();
}

uint32_t cfg_range_gen(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t v = s_range_gen;
    xSemaphoreGive(s_lock);
    return v;
}

int cfg_theme(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int v = s_theme;
    xSemaphoreGive(s_lock);
    return v;
}

void cfg_set_theme(int theme)
{
    if (theme < 0 || theme >= CFG_THEME_COUNT) theme = CFG_THEME_GENERIC;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = theme != s_theme;
    s_theme = theme;
    xSemaphoreGive(s_lock);
    if (!changed) return;
    persist();
    ESP_LOGI(TAG, "tema %d", theme);
}

bool cfg_show_map(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool v = s_show_map;
    xSemaphoreGive(s_lock);
    return v;
}

bool cfg_show_rivers(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool v = s_show_rivers;
    xSemaphoreGive(s_lock);
    return v;
}

void cfg_set_show_map(bool on)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = s_show_map != on;
    s_show_map = on;
    xSemaphoreGive(s_lock);
    if (!changed) return;
    persist();
    ESP_LOGI(TAG, "mapa %s", on ? "ligado" : "desligado");
}

void cfg_set_show_rivers(bool on)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = s_show_rivers != on;
    s_show_rivers = on;
    xSemaphoreGive(s_lock);
    if (!changed) return;
    persist();
    ESP_LOGI(TAG, "rios %s", on ? "ligado" : "desligado");
}

void cfg_zoom_in(void)
{
    cfg_set_range_index(cfg_range_index() - 1);
}

void cfg_zoom_out(void)
{
    cfg_set_range_index(cfg_range_index() + 1);
}
