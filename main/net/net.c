/* Consultas HTTPS: ADS-B (adsb.fi), rota e dono (adsbdb) e foto (planespotters,
   com miniatura do airport-data quando o planespotters nao tem a aeronave).
   A API recusa distancia acima de 250 NM (HTTP 400). O alcance para nesse valor.
   Com a tela apagada a tarefa nao consulta. O relogio NTP continua.
   00000000 nao e voo: a rota desse indicativo no adsbdb e falsa.
   Ritmo: adsb.fi 1/s, adsbdb bem abaixo de 512/min, planespotters 1/2s,
   airport-data 1/30s (120/h). Intervalo longo nao trava a varredura. */
#include "net.h"

#include "cfg.h"
#include "jpgd_rgb565.h"
#include "model.h"
#include "stat.h"
#include "wifi_app.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "net";

#define JSON_CAP (512 * 1024)
#define JPG_CAP  (120 * 1024)
/* O Planespotters recusa o pedido sem um contato no User-Agent. */
#define PHOTO_UA "AirRadar/1.0 (JC4880; +https://www.planespotters.net/photo/api)"

static uint8_t *s_json;
static plane_t *s_list;
static volatile int s_rush;
static volatile int s_dormant;
static int64_t s_detail_cycle = -1;
static int s_detail_n;
static uint8_t *s_jpg;
static uint16_t *s_photo_scratch;

enum {
    HTTP_OK = 0,
    HTTP_FAIL = 1,
    HTTP_EMPTY = 2,
    HTTP_LATER = 3
};
static int s_http_class;
static int s_http_ms;

enum {
    PACE_ADSB = 0,
    PACE_ADSBDB,
    PACE_PS,
    PACE_AIRPORT,
    PACE_N
};
/* adsb.fi publica 1/s. adsbdb bloqueia a partir de 512 em 60 s.
   Planespotters nao publica numero; 1 a cada 2 s evita rajada.
   Airport-Data: 120 pedidos por hora. */
static const int64_t k_pace_gap[PACE_N] = {
    1100000LL,
    1000000LL,
    2000000LL,
    30000000LL,
};
static int64_t s_pace_next[PACE_N];

static int pace_kind(const char *url)
{
    if (!url) return -1;
    if (strstr(url, "adsb.fi")) return PACE_ADSB;
    if (strstr(url, "adsbdb.com")) return PACE_ADSBDB;
    if (strstr(url, "api.planespotters.net")) return PACE_PS;
    if (strstr(url, "airport-data.com/api")) return PACE_AIRPORT;
    return -1;
}

static int64_t pace_wait_us(const char *url)
{
    int k = pace_kind(url);
    if (k < 0) return 0;
    int64_t wait = s_pace_next[k] - esp_timer_get_time();
    return wait > 0 ? wait : 0;
}

/* true = pode enviar. Espera no proprio pedido se o intervalo for curto. */
static bool pace_take(const char *url)
{
    int k = pace_kind(url);
    if (k < 0) return true;
    int64_t wait = pace_wait_us(url);
    if (wait > 2500000LL) return false;
    if (wait > 20000LL) vTaskDelay(pdMS_TO_TICKS((int)((wait + 999) / 1000)));
    s_pace_next[k] = esp_timer_get_time() + k_pace_gap[k];
    return true;
}

typedef struct {
    uint8_t *buf;
    int cap;
    int len;
    bool overflow;
} body_t;

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    body_t *b = evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0) {
        if (b->len + evt->data_len >= b->cap) {
            b->overflow = true;
        } else {
            memcpy(b->buf + b->len, evt->data, evt->data_len);
            b->len += evt->data_len;
            b->buf[b->len] = 0;
        }
    }
    return ESP_OK;
}

static bool http_get_ex(const char *url, uint8_t *buf, int cap, int *out_len, bool warn_404)
{
    s_http_class = HTTP_FAIL;
    s_http_ms = 0;
    if (!url || !buf || cap < 2) return false;
    if (!pace_take(url)) {
        s_http_class = HTTP_LATER;
        return false;
    }
    body_t body = {.buf = buf, .cap = cap, .len = 0, .overflow = false};
    buf[0] = 0;
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 12000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = PHOTO_UA,
        .event_handler = http_event,
        .user_data = &body,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = esp_http_client_perform(client);
    int ms = (int)((esp_timer_get_time() - t0) / 1000);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    {
        int kind = -1;
        int result = HTTP_FAIL;
        if (err == ESP_OK && status == 200 && !body.overflow) result = HTTP_OK;
        else if (status == 404) result = HTTP_EMPTY;
        s_http_class = result;
        s_http_ms = ms;
        if (status == 429) {
            int k = pace_kind(url);
            int64_t ban = esp_timer_get_time() + 30000000LL;
            if (k >= 0 && ban > s_pace_next[k]) s_pace_next[k] = ban;
        }
        if (strstr(url, "adsb.fi")) kind = STAT_ADSB;
        else if (strstr(url, "/callsign/")) kind = STAT_ROUTE;
        else if (strstr(url, "/aircraft/")) kind = STAT_OWNER;
        else if (strstr(url, "planespotters") || strstr(url, "plnspttrs") || strstr(url, "airport-data")) {
            kind = STAT_PHOTO;
        }
        if (kind >= 0) stat_http(kind, ms, result);
    }
    if (s_http_class == HTTP_OK) {
        if (out_len) *out_len = body.len;
        return true;
    }
    if (!(s_http_class == HTTP_EMPTY && !warn_404)) {
        ESP_LOGW(TAG, "GET %s -> %s status %d overflow %d", url, esp_err_to_name(err), status, body.overflow);
    }
    return false;
}

static bool http_get(const char *url, uint8_t *buf, int cap, int *out_len)
{
    return http_get_ex(url, buf, cap, out_len, true);
}

static const char *jstr(cJSON *o, const char *key)
{
    cJSON *i = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(i) ? i->valuestring : NULL;
}

static bool jnum(cJSON *o, const char *key, double *out)
{
    cJSON *i = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(i)) return false;
    *out = i->valuedouble;
    return true;
}

static int cmp_plane(const void *a, const void *b)
{
    const plane_t *pa = a;
    const plane_t *pb = b;
    if (pa->has_pos != pb->has_pos) return pa->has_pos ? -1 : 1;
    if (pa->dist_nm < pb->dist_nm) return -1;
    if (pa->dist_nm > pb->dist_nm) return 1;
    return 0;
}

static bool contains_ci(const char *s, const char *needle)
{
    if (!s || !needle || !needle[0]) return false;
    size_t nlen = strlen(needle);
    for (const char *p = s; *p; p++) {
        size_t i = 0;
        while (i < nlen) {
            char c = p[i];
            char d = needle[i];
            if (!c) return false;
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            if (d >= 'a' && d <= 'z') d = (char)(d - 32);
            if (c != d) break;
            i++;
        }
        if (i == nlen) return true;
    }
    return false;
}

static bool heli_type(const char *t)
{
    static const char *codes[] = {
        "R22", "R44", "R66", "B06", "B212", "B407", "B412", "B429", "B430",
        "AS32", "AS50", "AS55", "AS65", "A139", "A169", "A189",
        "EC20", "EC25", "EC30", "EC35", "EC45", "EC55", "EC75",
        "H60", "S76", "S92", "BK17", "H500", "MD52", "EN48", "GAZL",
        "ALO2", "ALO3", "EXPL", "PUMA", "LYNX", "B47G", "HUCO", "CABR",
    };
    if (!t || !t[0]) return false;
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        if (strcmp(t, codes[i]) == 0) return true;
    }
    return false;
}

static bool is_heli(const char *cat, const char *type, const char *desc)
{
    if (cat && strcmp(cat, "A7") == 0) return true;
    if (heli_type(type)) return true;
    return contains_ci(desc, "helicopter") || contains_ci(desc, "rotorcraft") ||
           contains_ci(desc, "eurocopter") || contains_ci(desc, "guimbal");
}

/* Em voo. Aviao: A0-A6 ou sem categoria. Helicoptero so se o filtro deixar.
   Fora: solo, planador, balao e veiculo de superficie. */
static bool keep_airborne(const char *cat, const char *type, const char *desc, bool on_ground,
                          bool want_plane, bool want_heli, bool *heli)
{
    *heli = false;
    if (on_ground) return false;
    if (is_heli(cat, type, desc)) {
        *heli = true;
        return want_heli;
    }
    if (!cat || !cat[0]) return want_plane;
    return want_plane && cat[0] == 'A' && cat[1] >= '0' && cat[1] <= '6' && cat[2] == '\0';
}

void net_request_rush(void)
{
    s_rush = 1;
}

void net_set_dormant(bool on)
{
    s_dormant = on ? 1 : 0;
    if (on) s_rush = 0;
}

static bool fetch_adsb(void)
{
    if (s_dormant) return false;
    uint32_t gen = cfg_gen();
    uint32_t rgen = cfg_range_gen();
    uint32_t kgen = cfg_kind_gen();
    uint8_t kinds = cfg_kinds();
    bool want_plane = (kinds & CFG_KIND_PLANE) != 0;
    bool want_heli = (kinds & CFG_KIND_HELI) != 0;
    double home_lat = cfg_lat();
    double home_lon = cfg_lon();
    float radius = cfg_range_nm();
    char url[160];
    snprintf(url, sizeof(url),
             "https://opendata.adsb.fi/api/v2/lat/%.6f/lon/%.6f/dist/%.1f",
             home_lat, home_lon, (double)radius);
    int len = 0;
    if (!http_get(url, s_json, JSON_CAP, &len)) return false;

    cJSON *root = cJSON_Parse((char *)s_json);
    if (!root) {
        ESP_LOGW(TAG, "JSON ADS-B invalido");
        return false;
    }
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "aircraft");
    if (!s_list) {
        cJSON_Delete(root);
        return false;
    }
    int count = 0;
    if (cJSON_IsArray(arr)) {
        cJSON *a;
        cJSON_ArrayForEach(a, arr) {
            const char *cat = jstr(a, "category");
            const char *type = jstr(a, "t");
            const char *desc = jstr(a, "desc");
            bool on_ground = false;
            cJSON *alt = cJSON_GetObjectItemCaseSensitive(a, "alt_baro");
            int alt_ft = -1;
            if (cJSON_IsNumber(alt)) alt_ft = (int)alt->valuedouble;
            else if (cJSON_IsString(alt) && alt->valuestring && strcmp(alt->valuestring, "ground") == 0) {
                alt_ft = 0;
                on_ground = true;
            }
            cJSON *ground = cJSON_GetObjectItemCaseSensitive(a, "ground");
            if (cJSON_IsTrue(ground)) on_ground = true;
            bool heli = false;
            if (!keep_airborne(cat, type, desc, on_ground, want_plane, want_heli, &heli)) continue;

            double lat = 0, lon = 0;
            if (!jnum(a, "lat", &lat) || !jnum(a, "lon", &lon) || (lat == 0 && lon == 0)) continue;

            plane_t scratch;
            memset(&scratch, 0, sizeof(scratch));
            scratch.track_deg = -1;
            scratch.alt_ft = alt_ft;
            scratch.gs_kt = -1;
            scratch.on_ground = false;
            scratch.heli = heli;
            scratch.has_pos = true;
            trim_copy(scratch.hex, sizeof(scratch.hex), jstr(a, "hex"));
            if (!scratch.hex[0]) continue;
            trim_copy(scratch.flight, sizeof(scratch.flight), jstr(a, "flight"));
            if (flight_is_placeholder(scratch.flight)) scratch.flight[0] = 0;
            trim_copy(scratch.reg, sizeof(scratch.reg), jstr(a, "r"));
            trim_copy(scratch.type_code, sizeof(scratch.type_code), type);
            trim_copy(scratch.desc, sizeof(scratch.desc), desc);
            ascii_fold(scratch.desc);
            if (!scratch.desc[0]) trim_copy(scratch.desc, sizeof(scratch.desc), scratch.type_code);
            airline_from_flight(scratch.flight, scratch.icao, sizeof(scratch.icao),
                                scratch.airline, sizeof(scratch.airline), &scratch.airline_color);
            model_apply_operator(&scratch);
            double gs = 0;
            if (jnum(a, "gs", &gs)) scratch.gs_kt = (int)gs;
            double track = 0;
            if (jnum(a, "track", &track)) scratch.track_deg = (float)track;
            geo_project_from(home_lat, home_lon, lat, lon, &scratch.dist_nm, &scratch.bearing_deg,
                             &scratch.east_nm, &scratch.north_nm);
            if (scratch.dist_nm > radius) continue;
            if (!model_airline_allowed(scratch.icao)) continue;

            if (count < PLANE_MAX) {
                s_list[count++] = scratch;
            } else {
                int far = 0;
                for (int i = 1; i < count; i++) {
                    if (s_list[i].dist_nm > s_list[far].dist_nm) far = i;
                }
                if (scratch.dist_nm < s_list[far].dist_nm) s_list[far] = scratch;
            }
        }
    }
    cJSON_Delete(root);
    if (s_dormant) {
        ESP_LOGI(TAG, "ADS-B descartado: tela desligada");
        return false;
    }
    if (cfg_gen() != gen || cfg_range_gen() != rgen || cfg_kind_gen() != kgen) {
        ESP_LOGI(TAG, "ADS-B descartado: o centro, o alcance ou o tipo mudou");
        return false;
    }
    qsort(s_list, (size_t)count, sizeof(plane_t), cmp_plane);
    model_stage_planes(s_list, count);
    int helis = 0;
    for (int i = 0; i < count; i++) {
        if (s_list[i].heli) helis++;
    }
    ESP_LOGI(TAG, "ADS-B cache: %d aeronaves (%d helicopteros) em %.0f NM", count, helis, radius);
    return true;
}

static void fetch_route(const char *flight)
{
    if (!flight || !flight[0]) return;
    int64_t t0 = esp_timer_get_time();
    char url[128];
    snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/callsign/%s", flight);
    int len = 0;
    if (!http_get_ex(url, s_json, JSON_CAP, &len, false)) {
        stat_trace_mark(NULL, flight, TRACE_ROUTE, s_http_ms, s_http_class, NULL);
        if (s_http_class == HTTP_EMPTY) model_route_miss(flight);
        else if (s_http_class == HTTP_LATER) model_route_hold(flight, pace_wait_us(url));
        else model_route_fail(flight);
        return;
    }
    cJSON *root = cJSON_Parse((char *)s_json);
    if (!root) {
        stat_trace_mark(NULL, flight, TRACE_ROUTE, s_http_ms, STAT_RES_FAIL, "json");
        model_route_fail(flight);
        return;
    }
    cJSON *route = cJSON_GetObjectItemCaseSensitive(root, "response");
    if (route) route = cJSON_GetObjectItemCaseSensitive(route, "flightroute");
    const char *origin = NULL;
    const char *dest = NULL;
    if (cJSON_IsObject(route)) {
        cJSON *o = cJSON_GetObjectItemCaseSensitive(route, "origin");
        cJSON *d = cJSON_GetObjectItemCaseSensitive(route, "destination");
        if (cJSON_IsObject(o)) {
            origin = jstr(o, "municipality");
            if (!origin) origin = jstr(o, "name");
        }
        if (cJSON_IsObject(d)) {
            dest = jstr(d, "municipality");
            if (!dest) dest = jstr(d, "name");
        }
    }
    if (origin && dest && origin[0] && dest[0]) {
        char note[36];
        snprintf(note, sizeof(note), "%.16s > %.16s", origin, dest);
        stat_trace_mark(NULL, flight, TRACE_ROUTE, s_http_ms, STAT_RES_OK, note);
        model_set_route(flight, origin, dest);
        char sel_flight[12];
        char sel_hex[8];
        char sel_reg[16];
        model_selected_ids(sel_flight, sizeof(sel_flight), sel_hex, sizeof(sel_hex), sel_reg, sizeof(sel_reg));
        ESP_LOGI(TAG, "rota %s em %d ms%s", flight,
                 (int)((esp_timer_get_time() - t0) / 1000),
                 (sel_flight[0] && strcmp(sel_flight, flight) == 0) ? "" : " prefetch");
    } else {
        stat_trace_mark(NULL, flight, TRACE_ROUTE, s_http_ms, STAT_RES_EMPTY, NULL);
        model_route_miss(flight);
        ESP_LOGW(TAG, "rota %s sem cidades", flight);
    }
    cJSON_Delete(root);
}

static void upper_copy(char *dst, int dst_n, const char *src)
{
    int i = 0;
    if (!dst || dst_n <= 0) return;
    if (!src) src = "";
    for (; src[i] && i < dst_n - 1; i++) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        dst[i] = c;
    }
    dst[i] = 0;
}

/* 1 = achou URL, 0 = catalogo sem foto, -1 = erro de rede, -2 = espera do limite. */
static int photo_lookup(const char *url, char *large, int large_n, char *small, int small_n,
                          char *credit, int credit_n)
{
    int len = 0;
    if (!http_get_ex(url, s_json, JSON_CAP, &len, false)) {
        if (s_http_class == HTTP_LATER) return -2;
        if (s_http_class == HTTP_EMPTY) return 0;
        return -1;
    }
    cJSON *root = cJSON_Parse((char *)s_json);
    if (!root) return -1;
    bool found = false;
    cJSON *photos = cJSON_GetObjectItemCaseSensitive(root, "photos");
    if (cJSON_IsArray(photos) && cJSON_GetArraySize(photos) > 0) {
        cJSON *ph = cJSON_GetArrayItem(photos, 0);
        cJSON *big = cJSON_GetObjectItemCaseSensitive(ph, "thumbnail_large");
        cJSON *th = cJSON_GetObjectItemCaseSensitive(ph, "thumbnail");
        const char *src_l = cJSON_IsObject(big) ? jstr(big, "src") : NULL;
        const char *src_s = cJSON_IsObject(th) ? jstr(th, "src") : NULL;
        const char *who = jstr(ph, "photographer");
        if (src_l && large && large_n) snprintf(large, large_n, "%s", src_l);
        if (src_s && small && small_n && (!src_l || strcmp(src_l, src_s) != 0)) {
            snprintf(small, small_n, "%s", src_s);
        }
        if (who && credit && credit_n && !credit[0]) snprintf(credit, credit_n, "%s", who);
        found = (src_l && src_l[0]) || (src_s && src_s[0]);
    }
    cJSON_Delete(root);
    return found ? 1 : 0;
}

/* Catalogo independente do Planespotters. Miniatura JPEG de paisagem, cerca de 200 px. */
static bool airport_data_lookup(const char *hex, const char *reg, char *image, int image_n,
                                char *credit, int credit_n)
{
    char hx[8];
    char rg[16];
    char url[160];
    int j = 0;
    upper_copy(hx, sizeof(hx), hex);
    if (!hx[0]) return false;
    rg[0] = 0;
    if (reg) {
        for (int i = 0; reg[i] && j < (int)sizeof(rg) - 1; i++) {
            char c = reg[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') rg[j++] = c;
        }
        rg[j] = 0;
    }
    if (rg[0]) {
        snprintf(url, sizeof(url), "https://airport-data.com/api/ac_thumb.json?m=%s&n=1&r=%s", hx, rg);
    } else {
        snprintf(url, sizeof(url), "https://airport-data.com/api/ac_thumb.json?m=%s&n=1", hx);
    }
    int len = 0;
    if (!http_get_ex(url, s_json, JSON_CAP, &len, false)) return false;
    cJSON *root = cJSON_Parse((char *)s_json);
    if (!root) return false;
    bool found = false;
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (cJSON_IsArray(data) && cJSON_GetArraySize(data) > 0) {
        cJSON *ph = cJSON_GetArrayItem(data, 0);
        const char *img = jstr(ph, "image");
        const char *who = jstr(ph, "photographer");
        if (img && img[0] && image && image_n) {
            snprintf(image, image_n, "%s", img);
            found = true;
        }
        if (who && credit && credit_n && !credit[0]) snprintf(credit, credit_n, "%s", who);
    }
    cJSON_Delete(root);
    return found;
}

static bool photo_decode(const char *url, int *w, int *h, int *http_ms, int *dec_ms)
{
    int len = 0;
    if (http_ms) *http_ms = 0;
    if (dec_ms) *dec_ms = -1;
    if (!url || !url[0] || !http_get_ex(url, s_jpg, JPG_CAP, &len, false)) {
        if (http_ms) *http_ms = s_http_ms;
        return false;
    }
    if (http_ms) *http_ms = s_http_ms;
    int64_t t1 = esp_timer_get_time();
    int cap = PHOTO_MAX_W * PHOTO_MAX_H * (int)sizeof(uint16_t);
    bool ok = jpgd_to_rgb565(s_jpg, len, s_photo_scratch, cap, PHOTO_MAX_W, PHOTO_MAX_H, w, h);
    if (dec_ms) *dec_ms = (int)((esp_timer_get_time() - t1) / 1000);
    if (!ok) ESP_LOGW(TAG, "jpeg da foto falhou");
    return ok;
}

static void fetch_operator(const char *hex, const char *reg)
{
    if (!hex || !hex[0] || !reg || !reg[0]) return;
    char url[160];
    snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/aircraft/%s", reg);
    int len = 0;
    if (!http_get_ex(url, s_json, JSON_CAP, &len, false)) {
        model_operator_miss(hex);
        return;
    }
    cJSON *root = cJSON_Parse((char *)s_json);
    if (!root) {
        model_operator_miss(hex);
        return;
    }
    cJSON *ac = cJSON_GetObjectItemCaseSensitive(root, "response");
    if (ac) ac = cJSON_GetObjectItemCaseSensitive(ac, "aircraft");
    const char *flag = cJSON_IsObject(ac) ? jstr(ac, "registered_owner_operator_flag_code") : NULL;
    char code[4] = {0};
    if (flag) {
        for (int k = 0; k < 3 && flag[k]; k++) {
            char c = flag[k];
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            if (c < 'A' || c > 'Z') break;
            code[k] = c;
        }
    }
    char name[16] = {0};
    uint32_t color = 0;
    if (code[0] && airline_from_icao(code, name, sizeof(name), &color)) {
        model_set_operator(hex, code, name, color);
        ESP_LOGI(TAG, "dono %s %s %s", hex, code, name);
    } else {
        model_operator_miss(hex);
    }
    cJSON_Delete(root);
}

static bool hex_is_selected(const char *hex)
{
    char flight[12];
    char sel[8];
    char reg[16];
    if (!hex || !hex[0]) return false;
    model_selected_ids(flight, sizeof(flight), sel, sizeof(sel), reg, sizeof(reg));
    return sel[0] && strcmp(sel, hex) == 0;
}

static void fetch_photo(const char *hex, const char *reg)
{
    if (!s_photo_scratch || !hex || !hex[0]) return;
    char large[320] = {0};
    char small[320] = {0};
    char credit_copy[48] = {0};
    char url[160];
    char id[16];
    int ps = 0;
    bool selected = hex_is_selected(hex);
    bool cached = model_photo_fresh(hex);
    /* A miniatura ja convertida aparece na hora. So a grande ainda e baixada. */
    if (cached && !model_photo_soft(hex)) return;
    if (cached && !selected) return;
    if (hex[0]) {
        upper_copy(id, sizeof(id), hex);
        snprintf(url, sizeof(url), "https://api.planespotters.net/pub/photos/hex/%s", id);
        ps = photo_lookup(url, large, sizeof(large), small, sizeof(small), credit_copy, sizeof(credit_copy));
        stat_trace_mark(hex, NULL, TRACE_PS_HEX, s_http_ms, s_http_class, NULL);
    }
    if (ps == 0 && reg && reg[0]) {
        int again;
        upper_copy(id, sizeof(id), reg);
        snprintf(url, sizeof(url), "https://api.planespotters.net/pub/photos/reg/%s", id);
        again = photo_lookup(url, large, sizeof(large), small, sizeof(small), credit_copy, sizeof(credit_copy));
        stat_trace_mark(hex, NULL, TRACE_PS_REG, s_http_ms, s_http_class, NULL);
        if (again == 1) ps = 1;
        else ps = again;
    }
    if (ps == -2) {
        model_photo_hold(hex, pace_wait_us("https://api.planespotters.net/"));
        return;
    }
    if (ps < 0 && !large[0] && !small[0] && !cached) {
        model_photo_fail(hex);
        return;
    }
    if (!large[0] && !small[0] && !cached) {
        const char *thumb = "https://airport-data.com/api/ac_thumb.json";
        int64_t wait = pace_wait_us(thumb);
        if (wait > 2500000LL) {
            model_photo_hold(hex, wait);
            return;
        }
        if (!airport_data_lookup(hex, reg, small, sizeof(small), credit_copy, sizeof(credit_copy))) {
            stat_trace_mark(hex, NULL, TRACE_AIRPORT, s_http_ms, s_http_class, NULL);
            if (s_http_class == HTTP_LATER) model_photo_hold(hex, pace_wait_us(thumb));
            else if (s_http_class == HTTP_FAIL) model_photo_fail(hex);
            else {
                ESP_LOGW(TAG, "foto %s sem miniatura", hex);
                model_photo_miss(hex);
            }
            return;
        }
        stat_trace_mark(hex, NULL, TRACE_AIRPORT, s_http_ms, STAT_RES_OK, "thumb");
    }
    if (cached && !large[0]) {
        if (ps < 0) model_photo_fail(hex);
        else model_photo_finish(hex);
        return;
    }
    int w = 0, h = 0;
    bool shown = cached;
    int http_ms = 0, dec_ms = -1;
    /* Quem ja esta em RGB565 nao converte de novo. A grande substitui no mesmo retangulo. */
    if (!cached && small[0]) {
        if (photo_decode(small, &w, &h, &http_ms, &dec_ms)) {
            model_photo_put(hex, s_photo_scratch, w, h, credit_copy, !large[0]);
            shown = true;
            stat_trace_mark(hex, NULL, TRACE_JPG_S, http_ms, STAT_RES_OK, NULL);
            stat_trace_mark(hex, NULL, TRACE_DEC_S, dec_ms, STAT_RES_OK, NULL);
            ESP_LOGI(TAG, "foto %s %dx%d http %d ms conv %d ms%s%s", hex, w, h, http_ms, dec_ms,
                     selected ? "" : " prefetch",
                     strstr(small, "airport-data.com") ? " airport-data" : "");
        } else {
            stat_trace_mark(hex, NULL, TRACE_JPG_S, http_ms, s_http_class, NULL);
            if (dec_ms >= 0) stat_trace_mark(hex, NULL, TRACE_DEC_S, dec_ms, STAT_RES_FAIL, NULL);
        }
    }
    if (large[0] && selected) {
        if (photo_decode(large, &w, &h, &http_ms, &dec_ms)) {
            model_photo_put(hex, s_photo_scratch, w, h, credit_copy, true);
            shown = true;
            stat_trace_mark(hex, NULL, TRACE_JPG_L, http_ms, STAT_RES_OK, NULL);
            stat_trace_mark(hex, NULL, TRACE_DEC_L, dec_ms, STAT_RES_OK, NULL);
            ESP_LOGI(TAG, "foto %s grande http %d ms conv %d ms", hex, http_ms, dec_ms);
        } else {
            stat_trace_mark(hex, NULL, TRACE_JPG_L, http_ms, s_http_class, NULL);
            if (dec_ms >= 0) stat_trace_mark(hex, NULL, TRACE_DEC_L, dec_ms, STAT_RES_FAIL, NULL);
            if (shown) {
                if (s_http_class == HTTP_FAIL || s_http_class == HTTP_LATER) model_photo_fail(hex);
                else model_photo_finish(hex);
                return;
            }
        }
    }
    if (shown) return;
    if (s_http_class == HTTP_FAIL || s_http_class == HTTP_LATER) model_photo_fail(hex);
    else model_photo_miss(hex);
}

static void on_time_sync(struct timeval *tv)
{
    (void)tv;
    ESP_LOGI(TAG, "hora NTP sincronizada");
}

static void clock_start(void)
{
    static bool started;
    if (started) return;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        3, ESP_SNTP_SERVER_LIST("a.ntp.br", "b.ntp.br", "pool.ntp.org"));
    cfg.wait_for_sync = false;
    cfg.sync_cb = on_time_sync;
    cfg.start = true;
    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NTP: %s", esp_err_to_name(err));
        return;
    }
    started = true;
    ESP_LOGI(TAG, "NTP iniciado");
}

static bool selected_pending(void)
{
    char flight[12] = {0};
    char hex[8] = {0};
    char reg[16] = {0};
    model_selected_ids(flight, sizeof(flight), hex, sizeof(hex), reg, sizeof(reg));
    if (flight[0] && model_route_needed(flight)) return true;
    if (hex[0] && reg[0] && flight_is_placeholder(flight) && model_operator_needed(hex)) return true;
    if (hex[0] && (model_photo_needed(hex) || model_photo_soft(hex))) return true;
    return false;
}

/* Uma rota ou uma foto, na folga antes da proxima consulta ADS-B.
   A escolhida vem antes do prefetch das outras. */
static bool fetch_one_detail(void)
{
    char flight[12] = {0};
    char hex[8] = {0};
    char reg[16] = {0};
    model_selected_ids(flight, sizeof(flight), hex, sizeof(hex), reg, sizeof(reg));
    if (flight[0] && model_route_needed(flight)) {
        fetch_route(flight);
        return true;
    }
    if (hex[0] && reg[0] && flight_is_placeholder(flight) && model_operator_needed(hex)) {
        fetch_operator(hex, reg);
        return true;
    }
    if (hex[0] && (model_photo_needed(hex) || model_photo_soft(hex))) {
        fetch_photo(hex, reg);
        return true;
    }
    char extra_flight[12];
    char extra_hex[8];
    char extra_reg[16];
    int kind = 0;
    if (!model_next_nearest(&kind, extra_flight, sizeof(extra_flight), extra_hex, sizeof(extra_hex),
                            extra_reg, sizeof(extra_reg))) {
        return false;
    }
    if (kind == MODEL_JOB_ROUTE) fetch_route(extra_flight);
    else if (kind == MODEL_JOB_PHOTO) fetch_photo(extra_hex, extra_reg);
    else if (kind == MODEL_JOB_OP) fetch_operator(extra_hex, extra_reg);
    else return false;
    return true;
}

static void fetch_adsb_timed(void)
{
    int64_t t0 = esp_timer_get_time();
    bool ok = fetch_adsb();
    int64_t dt = esp_timer_get_time() - t0;
    if (dt < 0) dt = 0;
    radar_sync_note_fetch_us(dt);
    stat_adsb_mark((int)(dt / 1000), ok ? STAT_RES_OK : (s_http_class == HTTP_EMPTY ? STAT_RES_EMPTY : STAT_RES_FAIL));
    if (!ok) return;
    if (!radar_sync_aligned()) {
        radar_sync_align_slack();
        ESP_LOGI(TAG, "varredura alinhada, consulta em %lld ms, folga 1 s", (long long)(dt / 1000));
    }
}

static void sleep_us(int64_t us)
{
    if (us < 20000) us = 20000;
    if (us > 15000000) us = 15000000;
    int64_t end = esp_timer_get_time() + us;
    while (esp_timer_get_time() < end) {
        if (s_rush || s_dormant) return;
        int64_t left = end - esp_timer_get_time();
        int nap = (int)(left / 1000);
        if (nap > 150) nap = 150;
        if (nap < 20) nap = 20;
        vTaskDelay(pdMS_TO_TICKS(nap));
    }
}

/* Uma consulta por giro. Se a resposta chegou antes da vertical, espera a linha passar. */
static void wait_out_this_cycle(int64_t cycle)
{
    int64_t deadline = esp_timer_get_time() + 130000000LL;
    while (!s_rush && !s_dormant && radar_sync_cycle() == cycle && esp_timer_get_time() < deadline) {
        int64_t until = radar_sync_us_until_vertical();
        int64_t nap = until + 80000LL;
        if (nap < 40000) nap = 40000;
        if (nap > 5000000) nap = 5000000;
        sleep_us(nap);
    }
}

static void net_task(void *arg)
{
    (void)arg;
    while (1) {
        if (!wifi_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(400));
            continue;
        }
        clock_start();
        if (s_dormant) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (s_rush) {
            uint32_t rgen = cfg_range_gen();
            s_rush = 0;
            bool ok = fetch_adsb();
            if (ok && cfg_range_gen() == rgen) {
                model_commit_staged();
                ESP_LOGI(TAG, "zoom out publicado sem esperar a varredura");
            }
            if (s_rush) continue;
            if (radar_sync_aligned()) wait_out_this_cycle(radar_sync_cycle());
            continue;
        }
        if (!radar_sync_aligned()) {
            fetch_adsb_timed();
            if (!radar_sync_aligned()) {
                vTaskDelay(pdMS_TO_TICKS(800));
                continue;
            }
            /* A primeira resposta alinha a vertical para daqui a cerca de 1 s. */
            wait_out_this_cycle(radar_sync_cycle());
            continue;
        }

        int64_t until = radar_sync_us_until_vertical();
        int64_t lead = radar_sync_lead_us();
        /* Dispara a API para ela terminar cerca de 1 s antes da linha na vertical.
           A aeronave escolhida nao espera o prefetch. O prefetch entra uma vez
           por giro, e so quando a escolhida ja tem rota e foto. */
        if (until > lead + 120000LL) {
            int64_t spare = until - lead;
            int64_t detail_cycle = radar_sync_cycle();
            if (detail_cycle != s_detail_cycle) {
                s_detail_cycle = detail_cycle;
                s_detail_n = 0;
            }
            if (selected_pending() && spare > 400000LL) {
                fetch_one_detail();
            } else if (!selected_pending() && s_detail_n < 1 && spare > 800000LL) {
                if (fetch_one_detail()) s_detail_n++;
                else s_detail_n = 1;
            }
            until = radar_sync_us_until_vertical();
            lead = radar_sync_lead_us();
            spare = until - lead;
            if (spare > 150000LL) {
                int64_t nap = spare > 200000LL ? 200000LL : spare;
                sleep_us(nap);
            }
            continue;
        }
        int64_t cycle = radar_sync_cycle();
        fetch_adsb_timed();
        wait_out_this_cycle(cycle);
    }
}

void net_start(void)
{
    s_json = heap_caps_malloc(JSON_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_jpg = heap_caps_malloc(JPG_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int cap = PHOTO_MAX_W * PHOTO_MAX_H * (int)sizeof(uint16_t);
    s_photo_scratch = heap_caps_aligned_alloc(16, cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_list = heap_caps_calloc(PLANE_MAX, sizeof(plane_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_json || !s_jpg || !s_list || !s_photo_scratch) {
        ESP_LOGE(TAG, "sem memoria para a rede");
        return;
    }
    /* jpgd deixa o decodificador na pilha (~16 KB). */
    xTaskCreate(net_task, "net", 49152, NULL, 4, NULL);
}
