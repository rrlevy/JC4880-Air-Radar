/* Lista de avioes, filtro, selecao, rotas, fotos e o relogio da varredura.
   A cor do ponto no radar e da companhia, nao do tema da tela. */
#include "model.h"

#include "cfg.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_sync;
static plane_t *s_planes;
static plane_t *s_staged;
static int s_staged_count;
static bool s_stage_ready;
static int64_t s_epoch_us;
static int64_t s_api_us;
static int64_t s_api_last_us;
static bool s_sync_aligned;
static int s_count;
static int s_sel;
static bool s_pinned;
static char s_pin_hex[8];
static uint32_t s_seq;
static int64_t s_refresh_us;

static const uint16_t *s_photo;
static int s_photo_w;
static int s_photo_h;
static char s_credit[48];
static char s_shown_hex[8];
static uint32_t s_photo_seq;

#define ROUTE_KEEP 64
#define ROUTE_TTL_US (3600LL * 1000000LL)
/* Erro de rede volta em 8 s. Resposta vazia espera mais, para nao martelar a API. */
#define ROUTE_FAIL_US (8LL * 1000000LL)
#define ROUTE_EMPTY_US (60LL * 1000000LL)
#define OP_KEEP 48
#define OP_TTL_US (24LL * 3600LL * 1000000LL)
#define OP_MISS_US (60LL * 1000000LL)
#define PHOTO_SLOTS 24
#define PHOTO_TTL_US (24LL * 3600LL * 1000000LL)
#define PHOTO_FAIL_US (8LL * 1000000LL)
#define PHOTO_EMPTY_US (600LL * 1000000LL)
#define PHOTO_MISS_N 32

typedef struct {
    char flight[12];
    char origin[40];
    char dest[40];
    int64_t ts;
    int64_t hold_us;
    bool hit;
} route_mem_t;

/* PSRAM: a RAM interna que sobra antes do escalonador cria a tarefa principal. */
static route_mem_t *s_route_mem;
static int s_route_next;
static int s_route_cursor;

typedef struct {
    char hex[8];
    char icao[4];
    char name[16];
    uint32_t color;
    bool hit;
    int64_t ts;
} op_mem_t;

static op_mem_t *s_op_mem;
static int s_op_next;
static int s_op_cursor;

typedef struct {
    char hex[8];
    uint16_t *px;
    int w;
    int h;
    char credit[48];
    int64_t stored_us;
    int64_t used_us;
    bool valid;
    bool full;
} photo_slot_t;

static photo_slot_t s_ph[PHOTO_SLOTS];
static int s_photo_cursor;

typedef struct {
    char hex[8];
    int64_t ts;
    int64_t window_us;
} photo_miss_t;

static photo_miss_t s_photo_miss[PHOTO_MISS_N];

typedef struct {
    const char *icao;
    const char *name;
    uint32_t color;
} airline_map_t;

/* Cores de marca usadas no cartao. GOL laranja oficial do brand book: #FF7020.
   LATAM: Chile LAN, Brasil TAM, Peru LPE, Equador LNE, Express LXP,
   Paraguai LAP, Colombia ARE, carga LCO, LTG e LAE. */
static const char *const k_latam[] = {
    "TAM", "LAN", "LPE", "LNE", "LXP", "LAP", "ARE", "LCO", "LTG", "LAE", NULL
};
static const char *const k_azul[] = {"AZU", "ACN", NULL};
/* Avianca (AVA), El Salvador (TAI), Costa Rica (LRC), Equador (GLG) e Wamos (PLM). */
static const char *const k_avianca[] = {"AVA", "TAI", "LRC", "GLG", NULL};

static const airline_map_t s_airlines[] = {
    {"GLO", "GOL",        0xFF7020},
    {"TAM", "LATAM",      0x1B0088},
    {"LAN", "LATAM",      0x1B0088},
    {"LPE", "LATAM",      0x1B0088},
    {"LNE", "LATAM",      0x1B0088},
    {"LXP", "LATAM",      0x1B0088},
    {"LAP", "LATAM",      0x1B0088},
    {"ARE", "LATAM",      0x1B0088},
    {"LCO", "LATAM",      0x1B0088},
    {"LTG", "LATAM",      0x1B0088},
    {"LAE", "LATAM",      0x1B0088},
    {"AZU", "AZUL",       0x0057B8},
    {"ACN", "AZUL",       0x0057B8},
    {"AVA", "AVIANCA",    0xE31837},
    {"TAI", "AVIANCA",    0xE31837},
    {"LRC", "AVIANCA",    0xE31837},
    {"GLG", "AVIANCA",    0xE31837},
    {"PLM", "WAMOS",      0x0B3A6A},
    {"TAP", "TAP",        0xE31C23},
    {"AAL", "AMERICAN",   0x0078D2},
    {"UAL", "UNITED",     0x002244},
    {"DAL", "DELTA",      0xC8102E},
    {"AFR", "AIR FRANCE", 0x002157},
    {"KLM", "KLM",        0x00A1DE},
    {"DLH", "LUFTHANSA",  0x05164D},
    {"IBE", "IBERIA",     0xD9001F},
    {"BAW", "BRITISH",    0x2E5C99},
    {"UAE", "EMIRATES",   0xD71921},
    {"QTR", "QATAR",      0x5C0632},
    {"THY", "TURKISH",    0xC70A0C},
    {"CMP", "COPA",       0x0033A0},
    {"ARG", "AEROLINEAS", 0x5BAFD7},
    {"ACA", "AIR CANADA", 0xF01428},
    {"AEA", "AIR EUROPA", 0x00A0E4},
    {"ITY", "ITA",        0x00643C},
    {"SWR", "SWISS",      0xE4002B},
    {"ETH", "ETHIOPIAN",  0x7A9B2E},
    {"AMX", "AEROMEXICO", 0x0B2343},
    {"BOV", "BOA",        0x0033A0},
    {"SKU", "SKY",        0x6F2C91},
    {"JAT", "JETSMART",   0x00A0E1},
    {"SID", "SIDERAL",    0xC8102E},
    {"TTL", "TOTAL",      0x0A3A6B},
};

void model_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_sync = xSemaphoreCreateMutex();
    s_sel = -1;
    s_epoch_us = esp_timer_get_time();
    s_planes = heap_caps_calloc(PLANE_MAX, sizeof(plane_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_staged = heap_caps_calloc(PLANE_MAX, sizeof(plane_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_route_mem = heap_caps_calloc(ROUTE_KEEP, sizeof(route_mem_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

static int64_t now_us(void) { return esp_timer_get_time(); }

void trim_copy(char *dst, int dst_n, const char *src)
{
    if (!dst || dst_n <= 0) return;
    dst[0] = 0;
    if (!src) return;
    while (*src == ' ') src++;
    snprintf(dst, dst_n, "%s", src);
    int n = (int)strlen(dst);
    while (n > 0 && (dst[n - 1] == ' ' || dst[n - 1] == '\r' || dst[n - 1] == '\n')) {
        dst[--n] = 0;
    }
}

void ascii_fold(char *s)
{
    if (!s) return;
    char *r = s;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (*p < 0x80) {
            *r++ = (char)(*p++);
            continue;
        }
        if (p[0] == 0xC3 && p[1]) {
            static const char map[] = {
                'A','A','A','A','A','A', 0, 'C','E','E','E','E','I','I','I','I',
                'D','N','O','O','O','O','O', 0, 'O','U','U','U','U','Y', 0, 0,
                'a','a','a','a','a','a', 0, 'c','e','e','e','e','i','i','i','i',
                'd','n','o','o','o','o','o', 0, 'o','u','u','u','u','y', 0, 'y'
            };
            unsigned char i = p[1] - 0x80;
            if (i < sizeof(map) && map[i]) *r++ = map[i];
            p += 2;
            continue;
        }
        if ((*p & 0xE0) == 0xC0) p += 2;
        else if ((*p & 0xF0) == 0xE0) p += 3;
        else if ((*p & 0xF8) == 0xF0) p += 4;
        else p++;
    }
    *r = 0;
}

void airline_from_flight(const char *flight, char *icao, int icao_n,
                         char *name, int name_n, uint32_t *color)
{
    char code[4] = {0};
    if (flight && !flight_is_placeholder(flight)) {
        int i = 0;
        while (flight[i] == ' ') i++;
        for (int k = 0; k < 3 && flight[i + k]; k++) {
            char c = flight[i + k];
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            if (c < 'A' || c > 'Z') break;
            code[k] = c;
        }
    }
    if (icao && icao_n > 0) snprintf(icao, icao_n, "%s", code);
    const airline_map_t *hit = NULL;
    for (size_t i = 0; i < sizeof(s_airlines) / sizeof(s_airlines[0]); i++) {
        if (strcmp(s_airlines[i].icao, code) == 0) {
            hit = &s_airlines[i];
            break;
        }
    }
    if (name && name_n > 0) {
        snprintf(name, name_n, "%s", hit ? hit->name : (code[0] ? code : "VOO"));
    }
    if (color) *color = hit ? hit->color : 0xFF7020;
}

bool airline_from_icao(const char *icao, char *name, int name_n, uint32_t *color)
{
    if (!icao || !icao[0]) return false;
    for (size_t i = 0; i < sizeof(s_airlines) / sizeof(s_airlines[0]); i++) {
        if (strcmp(s_airlines[i].icao, icao) == 0) {
            if (name && name_n > 0) snprintf(name, name_n, "%s", s_airlines[i].name);
            if (color) *color = s_airlines[i].color;
            return true;
        }
    }
    return false;
}

bool flight_is_placeholder(const char *flight)
{
    if (!flight || !flight[0]) return true;
    for (const char *p = flight; *p; p++) {
        if (*p != '0') return false;
    }
    return true;
}

static bool icao_in(const char *icao, const char *const *list)
{
    if (!icao || !icao[0] || !list) return false;
    for (int i = 0; list[i]; i++) {
        if (strcmp(icao, list[i]) == 0) return true;
    }
    return false;
}

static uint8_t airline_bit(const char *icao)
{
    if (!icao || !icao[0]) return CFG_FILT_OUTROS;
    if (strcmp(icao, "GLO") == 0) return CFG_FILT_GOL;
    if (icao_in(icao, k_latam)) return CFG_FILT_LATAM;
    if (icao_in(icao, k_azul)) return CFG_FILT_AZUL;
    return CFG_FILT_OUTROS;
}

static bool airline_passes(const char *icao, uint8_t filt)
{
    return (filt & airline_bit(icao)) != 0;
}

bool model_airline_allowed(const char *icao)
{
    return airline_passes(icao, cfg_filters());
}

uint32_t model_blip_color(const char *icao)
{
    if (icao && strcmp(icao, "GLO") == 0) return 0xFF7020;
    if (icao_in(icao, k_latam)) return 0xE10600;
    if (icao_in(icao, k_azul)) return 0x3D8BFF;
    if (icao_in(icao, k_avianca)) return 0xE31837;
    if (icao && strcmp(icao, "PLM") == 0) return 0x7A3E9D;
    return 0xD0D0D0;
}

void geo_project_from(double lat0, double lon0, double lat, double lon,
                      float *dist_nm, float *bearing_deg, float *east_nm, float *north_nm)
{
    const double lat1 = lat0 * (M_PI / 180.0);
    const double lon1 = lon0 * (M_PI / 180.0);
    const double lat2 = lat * (M_PI / 180.0);
    const double lon2 = lon * (M_PI / 180.0);
    const double dlat = lat2 - lat1;
    const double dlon = lon2 - lon1;
    const double h = sin(dlat / 2) * sin(dlat / 2) +
                     cos(lat1) * cos(lat2) * sin(dlon / 2) * sin(dlon / 2);
    double dist_m = 2 * 6371000.0 * asin(sqrt(h));
    float dist = (float)(dist_m / 1852.0);
    double y = sin(dlon) * cos(lat2);
    double x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dlon);
    double brg = atan2(y, x) * (180.0 / M_PI);
    if (brg < 0) brg += 360.0;
    if (dist_nm) *dist_nm = dist;
    if (bearing_deg) *bearing_deg = (float)brg;
    double rad = brg * (M_PI / 180.0);
    if (east_nm) *east_nm = (float)(dist * sin(rad));
    if (north_nm) *north_nm = (float)(dist * cos(rad));
}

static int nearest_locked(void)
{
    int sel = -1;
    for (int i = 0; i < s_count; i++) {
        if (!s_planes[i].has_pos) continue;
        if (sel < 0 || s_planes[i].dist_nm < s_planes[sel].dist_nm) sel = i;
    }
    return sel;
}

static int find_hex_locked(const char *hex)
{
    if (!hex || !hex[0]) return -1;
    for (int i = 0; i < s_count; i++) {
        if (!s_planes[i].has_pos) continue;
        if (strcmp(s_planes[i].hex, hex) == 0) return i;
    }
    return -1;
}

static const char *sel_hex_locked(void)
{
    if (s_sel < 0 || s_sel >= s_count) return "";
    return s_planes[s_sel].hex;
}

static route_mem_t *find_route_locked(const char *flight)
{
    if (!s_route_mem || !flight || !flight[0]) return NULL;
    for (int i = 0; i < ROUTE_KEEP; i++) {
        if (s_route_mem[i].flight[0] && strcmp(s_route_mem[i].flight, flight) == 0) {
            return &s_route_mem[i];
        }
    }
    return NULL;
}

static bool route_fresh_locked(const route_mem_t *m, int64_t now)
{
    return m && m->hit && m->origin[0] && m->dest[0] && (now - m->ts) < ROUTE_TTL_US;
}

static void remember_route_locked(const char *flight, const char *origin, const char *dest,
                                  bool hit, int64_t hold_us)
{
    if (!s_route_mem || !flight || !flight[0]) return;
    route_mem_t *m = find_route_locked(flight);
    if (!m) {
        m = &s_route_mem[s_route_next++ % ROUTE_KEEP];
        memset(m, 0, sizeof(*m));
        snprintf(m->flight, sizeof(m->flight), "%s", flight);
    }
    trim_copy(m->origin, sizeof(m->origin), origin ? origin : "");
    trim_copy(m->dest, sizeof(m->dest), dest ? dest : "");
    m->hit = hit && m->origin[0] && m->dest[0];
    m->hold_us = m->hit ? 0 : hold_us;
    m->ts = now_us();
}

static bool route_due_locked(const route_mem_t *m, int64_t now)
{
    if (!m) return true;
    int64_t age = now - m->ts;
    if (m->hit) return age >= ROUTE_TTL_US;
    int64_t hold = m->hold_us > 0 ? m->hold_us : ROUTE_EMPTY_US;
    return age >= hold;
}

static void route_hold_locked(const char *flight, int64_t us)
{
    if (!flight || !flight[0]) return;
    if (us < 1000000LL) us = 1000000LL;
    route_mem_t *m = find_route_locked(flight);
    if (m && m->hit) {
        int64_t keep = ROUTE_TTL_US - us;
        if (keep < 0) keep = 0;
        m->ts = now_us() - keep;
        return;
    }
    remember_route_locked(flight, "", "", false, us);
}

static void apply_routes_locked(void)
{
    int64_t now = now_us();
    for (int i = 0; i < s_count; i++) {
        route_mem_t *m = find_route_locked(s_planes[i].flight);
        if (!route_fresh_locked(m, now)) continue;
        snprintf(s_planes[i].origin, sizeof(s_planes[i].origin), "%s", m->origin);
        snprintf(s_planes[i].dest, sizeof(s_planes[i].dest), "%s", m->dest);
    }
}

static int find_photo_locked(const char *hex)
{
    if (!hex || !hex[0]) return -1;
    for (int i = 0; i < PHOTO_SLOTS; i++) {
        if (s_ph[i].px && strcmp(s_ph[i].hex, hex) == 0) return i;
    }
    return -1;
}

static bool photo_fresh_locked(const char *hex, int64_t now)
{
    int i = find_photo_locked(hex);
    if (i < 0 || !s_ph[i].valid) return false;
    return (now - s_ph[i].stored_us) < PHOTO_TTL_US;
}

static bool photo_miss_recent_locked(const char *hex, int64_t now)
{
    for (int i = 0; i < PHOTO_MISS_N; i++) {
        if (s_photo_miss[i].hex[0] && strcmp(s_photo_miss[i].hex, hex) == 0) {
            int64_t window = s_photo_miss[i].window_us > 0 ? s_photo_miss[i].window_us : PHOTO_EMPTY_US;
            return (now - s_photo_miss[i].ts) < window;
        }
    }
    return false;
}

static void note_photo_miss_locked(const char *hex, int64_t window_us)
{
    int slot = -1;
    int64_t oldest = 0;
    if (window_us < 1000000LL) window_us = 1000000LL;
    for (int i = 0; i < PHOTO_MISS_N; i++) {
        if (strcmp(s_photo_miss[i].hex, hex) == 0) {
            s_photo_miss[i].ts = now_us();
            s_photo_miss[i].window_us = window_us;
            return;
        }
        if (!s_photo_miss[i].hex[0]) {
            slot = i;
            break;
        }
        if (slot < 0 || s_photo_miss[i].ts < oldest) {
            slot = i;
            oldest = s_photo_miss[i].ts;
        }
    }
    if (slot < 0) slot = 0;
    memset(&s_photo_miss[slot], 0, sizeof(s_photo_miss[slot]));
    snprintf(s_photo_miss[slot].hex, sizeof(s_photo_miss[slot].hex), "%s", hex);
    s_photo_miss[slot].ts = now_us();
    s_photo_miss[slot].window_us = window_us;
}

static void clear_photo_miss_locked(const char *hex)
{
    for (int i = 0; i < PHOTO_MISS_N; i++) {
        if (strcmp(s_photo_miss[i].hex, hex) == 0) s_photo_miss[i].hex[0] = 0;
    }
}

/* Troca o que esta na tela. Limpa antes e, se o cache estiver fresco, aplica na mesma chamada. */
static void attach_visual_locked(void)
{
    const char *hex = sel_hex_locked();
    bool same = strcmp(s_shown_hex, hex) == 0;
    if (s_sel >= 0 && s_sel < s_count) {
        int64_t now = now_us();
        route_mem_t *m = find_route_locked(s_planes[s_sel].flight);
        if (route_fresh_locked(m, now)) {
            snprintf(s_planes[s_sel].origin, sizeof(s_planes[s_sel].origin), "%s", m->origin);
            snprintf(s_planes[s_sel].dest, sizeof(s_planes[s_sel].dest), "%s", m->dest);
        } else if (!same) {
            s_planes[s_sel].origin[0] = 0;
            s_planes[s_sel].dest[0] = 0;
        }
    }
    if (same && s_photo) return;

    if (!same) {
        s_photo = NULL;
        s_photo_w = 0;
        s_photo_h = 0;
        s_credit[0] = 0;
        snprintf(s_shown_hex, sizeof(s_shown_hex), "%s", hex);
        s_photo_seq++;
    }
    if (!hex[0]) return;

    int64_t now = now_us();
    int i = find_photo_locked(hex);
    if (i >= 0 && s_ph[i].valid && (now - s_ph[i].stored_us) < PHOTO_TTL_US) {
        s_photo = s_ph[i].px;
        s_photo_w = s_ph[i].w;
        s_photo_h = s_ph[i].h;
        snprintf(s_credit, sizeof(s_credit), "%s", s_ph[i].credit);
        s_ph[i].used_us = now;
        s_photo_seq++;
    }
}

static void choose_sel_locked(void)
{
    if (s_pinned) {
        int keep = find_hex_locked(s_pin_hex);
        if (keep >= 0) {
            s_sel = keep;
            attach_visual_locked();
            return;
        }
        s_pinned = false;
        s_pin_hex[0] = 0;
    }
    s_sel = nearest_locked();
    attach_visual_locked();
}

static bool keep_plane(const plane_t *p, float range, uint8_t filt, uint8_t kind)
{
    if (!p->has_pos) return false;
    if (p->dist_nm > range) return false;
    if (!airline_passes(p->icao, filt)) return false;
    uint8_t bit = p->heli ? CFG_KIND_HELI : CFG_KIND_PLANE;
    if ((kind & bit) == 0) return false;
    return true;
}

static int filter_list(plane_t *dst, const plane_t *src, int count, float range, uint8_t filt, uint8_t kind)
{
    int w = 0;
    if (!src || count <= 0) return 0;
    if (count > PLANE_MAX) count = PLANE_MAX;
    for (int i = 0; i < count; i++) {
        if (!keep_plane(&src[i], range, filt, kind)) continue;
        if (dst != src || w != i) dst[w] = src[i];
        w++;
    }
    return w;
}

static int64_t sync_period_us(void)
{
    int p = cfg_period_s();
    if (p < 2) p = 2;
    return (int64_t)p * 1000000LL;
}

static int64_t sync_phase_us(int64_t now, int64_t epoch, int64_t period)
{
    int64_t ph = (now - epoch) % period;
    if (ph < 0) ph += period;
    return ph;
}

void radar_sync_note_fetch_us(int64_t elapsed_us)
{
    if (elapsed_us < 50000) elapsed_us = 50000;
    if (elapsed_us > 20000000) elapsed_us = 20000000;
    if (!s_sync) return;
    xSemaphoreTake(s_sync, portMAX_DELAY);
    s_api_last_us = elapsed_us;
    if (s_api_us <= 0) s_api_us = elapsed_us;
    else s_api_us = (s_api_us * 3 + elapsed_us) / 4;
    xSemaphoreGive(s_sync);
}

int64_t radar_sync_lead_us(void)
{
    int64_t period = sync_period_us();
    int64_t api = 0;
    int64_t last = 0;
    if (s_sync) {
        xSemaphoreTake(s_sync, portMAX_DELAY);
        api = s_api_us;
        last = s_api_last_us;
        xSemaphoreGive(s_sync);
    }
    int64_t delay = api > last ? api : last;
    if (delay <= 0) delay = 1000000LL;
    int64_t lead = delay + 1000000LL;
    int64_t max_lead = period - 250000LL;
    if (max_lead < 200000LL) max_lead = period / 2;
    if (lead > max_lead) lead = max_lead;
    if (lead < 200000LL) lead = 200000LL;
    return lead;
}

int64_t radar_sync_us_until_vertical(void)
{
    int64_t period = sync_period_us();
    int64_t epoch = 0;
    if (s_sync) {
        xSemaphoreTake(s_sync, portMAX_DELAY);
        epoch = s_epoch_us;
        xSemaphoreGive(s_sync);
    }
    int64_t ph = sync_phase_us(esp_timer_get_time(), epoch, period);
    int64_t until = period - ph;
    if (until <= 0) until = period;
    return until;
}

float radar_sync_sweep_deg(void)
{
    int64_t period = sync_period_us();
    int64_t epoch = 0;
    if (s_sync) {
        xSemaphoreTake(s_sync, portMAX_DELAY);
        epoch = s_epoch_us;
        xSemaphoreGive(s_sync);
    }
    int64_t ph = sync_phase_us(esp_timer_get_time(), epoch, period);
    return (float)ph * 360.0f / (float)period;
}

int radar_sync_age_s(void)
{
    int64_t period = sync_period_us();
    int64_t epoch = 0;
    if (s_sync) {
        xSemaphoreTake(s_sync, portMAX_DELAY);
        epoch = s_epoch_us;
        xSemaphoreGive(s_sync);
    }
    int age = (int)(sync_phase_us(esp_timer_get_time(), epoch, period) / 1000000LL);
    if (age < 0) age = 0;
    if (age > 99) age = 99;
    return age;
}

void radar_sync_align_slack(void)
{
    int64_t period = sync_period_us();
    int64_t slack = 1000000LL;
    if (slack > period - 200000LL) slack = period / 4;
    if (slack < 100000LL) slack = 100000LL;
    int64_t now = esp_timer_get_time();
    if (!s_sync) return;
    xSemaphoreTake(s_sync, portMAX_DELAY);
    s_epoch_us = now - (period - slack);
    s_sync_aligned = true;
    xSemaphoreGive(s_sync);
}

bool radar_sync_aligned(void)
{
    if (!s_sync) return false;
    xSemaphoreTake(s_sync, portMAX_DELAY);
    bool v = s_sync_aligned;
    xSemaphoreGive(s_sync);
    return v;
}

int64_t radar_sync_cycle(void)
{
    int64_t period = sync_period_us();
    int64_t epoch = 0;
    if (s_sync) {
        xSemaphoreTake(s_sync, portMAX_DELAY);
        epoch = s_epoch_us;
        xSemaphoreGive(s_sync);
    }
    int64_t delta = esp_timer_get_time() - epoch;
    if (delta >= 0) return delta / period;
    return -(((-delta) + period - 1) / period);
}

void model_set_planes(const plane_t *list, int count)
{
    if (count < 0) count = 0;
    if (count > PLANE_MAX) count = PLANE_MAX;
    if (!s_planes || !s_lock) return;
    float range = cfg_range_nm();
    uint8_t filt = cfg_filters();
    uint8_t kind = cfg_kinds();
    lock();
    s_count = filter_list(s_planes, list, count, range, filt, kind);
    apply_routes_locked();
    choose_sel_locked();
    s_seq++;
    unlock();
}

void model_stage_planes(const plane_t *list, int count)
{
    if (count < 0) count = 0;
    if (count > PLANE_MAX) count = PLANE_MAX;
    if (!s_staged || !s_lock) return;
    float range = cfg_range_nm();
    uint8_t filt = cfg_filters();
    uint8_t kind = cfg_kinds();
    lock();
    s_staged_count = filter_list(s_staged, list, count, range, filt, kind);
    s_stage_ready = true;
    unlock();
}

bool model_commit_staged(void)
{
    if (!s_planes || !s_staged || !s_lock) return false;
    float range = cfg_range_nm();
    uint8_t filt = cfg_filters();
    uint8_t kind = cfg_kinds();
    lock();
    if (!s_stage_ready) {
        unlock();
        return false;
    }
    s_count = filter_list(s_planes, s_staged, s_staged_count, range, filt, kind);
    s_stage_ready = false;
    apply_routes_locked();
    choose_sel_locked();
    s_seq++;
    unlock();
    return true;
}

void model_clear(void)
{
    if (!s_lock || !s_planes) return;
    lock();
    s_count = 0;
    s_staged_count = 0;
    s_stage_ready = false;
    s_sel = -1;
    s_pinned = false;
    s_pin_hex[0] = 0;
    attach_visual_locked();
    s_seq++;
    unlock();
}

void model_refilter(void)
{
    if (!s_lock || !s_planes) return;
    float range = cfg_range_nm();
    uint8_t filt = cfg_filters();
    uint8_t kind = cfg_kinds();
    lock();
    s_count = filter_list(s_planes, s_planes, s_count, range, filt, kind);
    if (s_stage_ready && s_staged) {
        s_staged_count = filter_list(s_staged, s_staged, s_staged_count, range, filt, kind);
    }
    choose_sel_locked();
    s_seq++;
    unlock();
}

int model_pick_copy(model_pick_t *out, int max)
{
    if (!out || max <= 0) return 0;
    lock();
    if (!s_planes) {
        unlock();
        return 0;
    }
    int n = s_count;
    if (n > max) n = max;
    int64_t now = now_us();
    for (int i = 0; i < n; i++) {
        const plane_t *p = &s_planes[i];
        snprintf(out[i].hex, sizeof(out[i].hex), "%s", p->hex);
        snprintf(out[i].icao, sizeof(out[i].icao), "%s", p->icao);
        float d = p->dist_nm;
        if (d < 0.f) d = 0.f;
        out[i].dist_x10 = (int)(d * 10.f + 0.5f);
        out[i].photo = p->hex[0] && photo_fresh_locked(p->hex, now);
    }
    unlock();
    return n;
}

int model_copy(plane_t *out, int max, int *selected_index)
{
    lock();
    if (!s_planes) {
        if (selected_index) *selected_index = -1;
        unlock();
        return 0;
    }
    int n = s_count;
    if (n > max) n = max;
    if (out && n) memcpy(out, s_planes, (size_t)n * sizeof(plane_t));
    if (selected_index) *selected_index = s_sel;
    unlock();
    return n;
}

static void pin_locked(int index)
{
    if (index < 0 || index >= s_count || !s_planes[index].has_pos) return;
    s_sel = index;
    s_pinned = true;
    snprintf(s_pin_hex, sizeof(s_pin_hex), "%s", s_planes[index].hex);
    attach_visual_locked();
    s_seq++;
}

void model_select_index(int index)
{
    lock();
    pin_locked(index);
    unlock();
}

void model_select_hex(const char *hex)
{
    lock();
    pin_locked(find_hex_locked(hex));
    unlock();
}

void model_release_pin(void)
{
    if (!s_lock) return;
    lock();
    bool was = s_pinned;
    int next = nearest_locked();
    s_pinned = false;
    s_pin_hex[0] = 0;
    if (!was && s_sel == next) {
        unlock();
        return;
    }
    s_sel = next;
    attach_visual_locked();
    s_seq++;
    unlock();
}

uint32_t model_seq(void)
{
    lock();
    uint32_t v = s_seq;
    unlock();
    return v;
}

void model_set_route(const char *flight, const char *origin, const char *dest)
{
    if (!flight || !flight[0]) return;
    lock();
    char o[40], d[40];
    trim_copy(o, sizeof(o), origin ? origin : "");
    trim_copy(d, sizeof(d), dest ? dest : "");
    ascii_fold(o);
    ascii_fold(d);
    bool hit = o[0] && d[0];
    remember_route_locked(flight, o, d, hit, 0);
    if (hit) {
        for (int i = 0; i < s_count; i++) {
            if (strcmp(s_planes[i].flight, flight) == 0) {
                snprintf(s_planes[i].origin, sizeof(s_planes[i].origin), "%s", o);
                snprintf(s_planes[i].dest, sizeof(s_planes[i].dest), "%s", d);
            }
        }
        s_seq++;
    }
    unlock();
}

void model_route_miss(const char *flight)
{
    if (!flight || !flight[0]) return;
    lock();
    remember_route_locked(flight, "", "", false, ROUTE_EMPTY_US);
    unlock();
}

void model_route_fail(const char *flight)
{
    if (!flight || !flight[0]) return;
    lock();
    route_hold_locked(flight, ROUTE_FAIL_US);
    unlock();
}

void model_route_hold(const char *flight, int64_t us)
{
    if (!flight || !flight[0]) return;
    lock();
    route_hold_locked(flight, us);
    unlock();
}

bool model_route_needed(const char *flight)
{
    if (!flight || !flight[0] || flight_is_placeholder(flight)) return false;
    lock();
    bool need = route_due_locked(find_route_locked(flight), now_us());
    unlock();
    return need;
}

bool model_next_route(char *flight, int flight_n)
{
    if (!flight || flight_n <= 0) return false;
    flight[0] = 0;
    lock();
    if (s_count <= 0) {
        unlock();
        return false;
    }
    int64_t now = now_us();
    for (int k = 0; k < s_count; k++) {
        int i = (s_route_cursor + k) % s_count;
        const char *f = s_planes[i].flight;
        if (!f[0] || flight_is_placeholder(f)) continue;
        if (!route_due_locked(find_route_locked(f), now)) continue;
        snprintf(flight, flight_n, "%s", f);
        s_route_cursor = (i + 1) % s_count;
        unlock();
        return true;
    }
    unlock();
    return false;
}

static op_mem_t *find_op_locked(const char *hex)
{
    if (!s_op_mem || !hex || !hex[0]) return NULL;
    for (int i = 0; i < OP_KEEP; i++) {
        if (s_op_mem[i].hex[0] && strcmp(s_op_mem[i].hex, hex) == 0) return &s_op_mem[i];
    }
    return NULL;
}

static bool op_ready_locked(void)
{
    if (s_op_mem) return true;
    s_op_mem = heap_caps_calloc(OP_KEEP, sizeof(op_mem_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s_op_mem != NULL;
}

static bool operator_fresh_locked(const op_mem_t *m, int64_t now)
{
    if (!m) return false;
    if (m->hit) return (now - m->ts) < OP_TTL_US;
    return (now - m->ts) < OP_MISS_US;
}

static void paint_operator_locked(plane_t *p, const op_mem_t *m)
{
    if (!p || !m || !m->hit || p->icao[0]) return;
    snprintf(p->icao, sizeof(p->icao), "%s", m->icao);
    snprintf(p->airline, sizeof(p->airline), "%s", m->name);
    p->airline_color = m->color;
}

void model_apply_operator(plane_t *p)
{
    if (!p || !p->hex[0] || p->icao[0]) return;
    lock();
    op_mem_t *m = find_op_locked(p->hex);
    if (operator_fresh_locked(m, now_us())) paint_operator_locked(p, m);
    unlock();
}

void model_set_operator(const char *hex, const char *icao, const char *name, uint32_t color)
{
    if (!hex || !hex[0] || !icao || !icao[0] || !name || !name[0]) return;
    lock();
    if (!op_ready_locked()) {
        unlock();
        return;
    }
    op_mem_t *m = find_op_locked(hex);
    if (!m) {
        m = &s_op_mem[s_op_next++ % OP_KEEP];
        memset(m, 0, sizeof(*m));
        snprintf(m->hex, sizeof(m->hex), "%s", hex);
    }
    snprintf(m->icao, sizeof(m->icao), "%s", icao);
    snprintf(m->name, sizeof(m->name), "%s", name);
    m->color = color;
    m->hit = true;
    m->ts = now_us();
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_planes[i].hex, hex) == 0) paint_operator_locked(&s_planes[i], m);
    }
    s_seq++;
    unlock();
}

void model_operator_miss(const char *hex)
{
    if (!hex || !hex[0]) return;
    lock();
    if (!op_ready_locked()) {
        unlock();
        return;
    }
    op_mem_t *m = find_op_locked(hex);
    if (!m) {
        m = &s_op_mem[s_op_next++ % OP_KEEP];
        memset(m, 0, sizeof(*m));
        snprintf(m->hex, sizeof(m->hex), "%s", hex);
    }
    m->hit = false;
    m->icao[0] = 0;
    m->name[0] = 0;
    m->ts = now_us();
    unlock();
}

bool model_operator_needed(const char *hex)
{
    if (!hex || !hex[0]) return false;
    lock();
    bool need = !operator_fresh_locked(find_op_locked(hex), now_us());
    unlock();
    return need;
}

bool model_next_operator(char *hex, int hex_n, char *reg, int reg_n)
{
    if (hex && hex_n) hex[0] = 0;
    if (reg && reg_n) reg[0] = 0;
    lock();
    if (s_count <= 0) {
        unlock();
        return false;
    }
    int64_t now = now_us();
    for (int k = 0; k < s_count; k++) {
        int i = (s_op_cursor + k) % s_count;
        plane_t *p = &s_planes[i];
        if (p->icao[0] || !p->reg[0] || !flight_is_placeholder(p->flight)) {
            continue;
        }
        if (operator_fresh_locked(find_op_locked(p->hex), now)) continue;
        if (hex && hex_n) snprintf(hex, hex_n, "%s", p->hex);
        if (reg && reg_n) snprintf(reg, reg_n, "%s", p->reg);
        s_op_cursor = (i + 1) % s_count;
        unlock();
        return true;
    }
    unlock();
    return false;
}

void model_mark_refresh(void)
{
    lock();
    s_refresh_us = esp_timer_get_time();
    unlock();
}

int model_seconds_since_refresh(void)
{
    lock();
    int64_t t = s_refresh_us;
    unlock();
    if (t <= 0) return -1;
    int64_t d = (esp_timer_get_time() - t) / 1000000;
    if (d < 0) d = 0;
    if (d > 9999) d = 9999;
    return (int)d;
}

bool model_selected_ids(char *flight, int flight_n, char *hex, int hex_n, char *reg, int reg_n)
{
    lock();
    bool ok = s_sel >= 0 && s_sel < s_count;
    if (ok) {
        if (flight && flight_n) snprintf(flight, flight_n, "%s", s_planes[s_sel].flight);
        if (hex && hex_n) snprintf(hex, hex_n, "%s", s_planes[s_sel].hex);
        if (reg && reg_n) snprintf(reg, reg_n, "%s", s_planes[s_sel].reg);
    } else {
        if (flight && flight_n) flight[0] = 0;
        if (hex && hex_n) hex[0] = 0;
        if (reg && reg_n) reg[0] = 0;
    }
    unlock();
    return ok;
}

bool model_next_nearest(int *kind, char *flight, int flight_n, char *hex, int hex_n, char *reg, int reg_n)
{
    if (kind) *kind = 0;
    if (flight && flight_n) flight[0] = 0;
    if (hex && hex_n) hex[0] = 0;
    if (reg && reg_n) reg[0] = 0;
    lock();
    int best = -1;
    int64_t now = now_us();
    for (int i = 0; i < s_count; i++) {
        plane_t *p = &s_planes[i];
        bool route = p->flight[0] && !flight_is_placeholder(p->flight)
                     && route_due_locked(find_route_locked(p->flight), now);
        bool photo = p->hex[0] && !photo_fresh_locked(p->hex, now) && !photo_miss_recent_locked(p->hex, now);
        bool op = p->hex[0] && p->reg[0] && !p->icao[0] && flight_is_placeholder(p->flight)
                  && !operator_fresh_locked(find_op_locked(p->hex), now);
        if (!route && !photo && !op) continue;
        if (best < 0 || p->dist_nm < s_planes[best].dist_nm) best = i;
    }
    if (best < 0) {
        unlock();
        return false;
    }
    plane_t *p = &s_planes[best];
    int job = 0;
    if (p->flight[0] && !flight_is_placeholder(p->flight) && route_due_locked(find_route_locked(p->flight), now)) {
        job = MODEL_JOB_ROUTE;
    } else if (p->hex[0] && !photo_fresh_locked(p->hex, now) && !photo_miss_recent_locked(p->hex, now)) {
        job = MODEL_JOB_PHOTO;
    } else {
        job = MODEL_JOB_OP;
    }
    if (kind) *kind = job;
    if (flight && flight_n) snprintf(flight, flight_n, "%s", p->flight);
    if (hex && hex_n) snprintf(hex, hex_n, "%s", p->hex);
    if (reg && reg_n) snprintf(reg, reg_n, "%s", p->reg);
    unlock();
    return job != 0;
}

bool model_debug_selected(model_dbg_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    out->alt_ft = -1;
    out->gs_kt = -1;
    out->track_deg = -1;
    lock();
    bool ok = s_sel >= 0 && s_sel < s_count;
    if (ok) {
        plane_t *p = &s_planes[s_sel];
        snprintf(out->hex, sizeof(out->hex), "%s", p->hex);
        snprintf(out->flight, sizeof(out->flight), "%s", p->flight);
        snprintf(out->reg, sizeof(out->reg), "%s", p->reg);
        snprintf(out->type_code, sizeof(out->type_code), "%s", p->type_code);
        snprintf(out->desc, sizeof(out->desc), "%s", p->desc);
        snprintf(out->origin, sizeof(out->origin), "%s", p->origin);
        snprintf(out->dest, sizeof(out->dest), "%s", p->dest);
        snprintf(out->airline, sizeof(out->airline), "%s", p->airline);
        out->alt_ft = p->alt_ft;
        out->gs_kt = p->gs_kt;
        out->dist_nm = p->dist_nm;
        out->track_deg = p->track_deg;
        int i = find_photo_locked(p->hex);
        if (i >= 0 && s_ph[i].valid && (now_us() - s_ph[i].stored_us) < PHOTO_TTL_US) {
            out->photo_cached = true;
            out->photo_full = s_ph[i].full;
            out->photo_w = s_ph[i].w;
            out->photo_h = s_ph[i].h;
        }
    }
    unlock();
    return ok;
}

static int pick_photo_slot_locked(const char *hex)
{
    int found = find_photo_locked(hex);
    if (found >= 0) return found;
    for (int i = 0; i < PHOTO_SLOTS; i++) {
        if (!s_ph[i].px) return i;
    }
    int best = -1;
    for (int i = 0; i < PHOTO_SLOTS; i++) {
        if (s_photo && s_ph[i].px == s_photo) continue;
        if (best < 0 || s_ph[i].used_us < s_ph[best].used_us) best = i;
    }
    return best;
}

void model_photo_put(const char *hex, const uint16_t *rgb565, int w, int h, const char *credit, bool full)
{
    if (!hex || !hex[0] || !rgb565 || w <= 0 || h <= 0) return;
    if (w > PHOTO_MAX_W || h > PHOTO_MAX_H) return;
    lock();
    int i = pick_photo_slot_locked(hex);
    if (i < 0) {
        unlock();
        return;
    }
    if (!s_ph[i].px) {
        s_ph[i].px = heap_caps_aligned_alloc(16, (size_t)PHOTO_MAX_W * PHOTO_MAX_H * sizeof(uint16_t),
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_ph[i].px) {
            unlock();
            return;
        }
    }
    if (s_ph[i].valid && s_ph[i].full && !full && strcmp(s_ph[i].hex, hex) == 0) {
        unlock();
        return;
    }
    memcpy(s_ph[i].px, rgb565, (size_t)w * (size_t)h * sizeof(uint16_t));
    snprintf(s_ph[i].hex, sizeof(s_ph[i].hex), "%s", hex);
    s_ph[i].w = w;
    s_ph[i].h = h;
    s_ph[i].valid = true;
    s_ph[i].full = full;
    trim_copy(s_ph[i].credit, sizeof(s_ph[i].credit), credit ? credit : "");
    ascii_fold(s_ph[i].credit);
    int64_t now = now_us();
    s_ph[i].stored_us = now;
    s_ph[i].used_us = now;
    clear_photo_miss_locked(hex);
    if (strcmp(sel_hex_locked(), hex) == 0) {
        s_photo = s_ph[i].px;
        s_photo_w = w;
        s_photo_h = h;
        snprintf(s_credit, sizeof(s_credit), "%s", s_ph[i].credit);
        snprintf(s_shown_hex, sizeof(s_shown_hex), "%s", hex);
        s_photo_seq++;
        s_seq++;
    }
    unlock();
}

void model_photo_finish(const char *hex)
{
    if (!hex || !hex[0]) return;
    lock();
    int i = find_photo_locked(hex);
    if (i >= 0 && s_ph[i].valid) s_ph[i].full = true;
    clear_photo_miss_locked(hex);
    unlock();
}

void model_photo_miss(const char *hex)
{
    if (!hex || !hex[0]) return;
    lock();
    note_photo_miss_locked(hex, PHOTO_EMPTY_US);
    unlock();
}

void model_photo_fail(const char *hex)
{
    if (!hex || !hex[0]) return;
    lock();
    note_photo_miss_locked(hex, PHOTO_FAIL_US);
    unlock();
}

void model_photo_hold(const char *hex, int64_t us)
{
    if (!hex || !hex[0]) return;
    lock();
    note_photo_miss_locked(hex, us);
    unlock();
}

bool model_photo_fresh(const char *hex)
{
    lock();
    bool ok = photo_fresh_locked(hex, now_us());
    unlock();
    return ok;
}

bool model_photo_needed(const char *hex)
{
    if (!hex || !hex[0]) return false;
    lock();
    int64_t now = now_us();
    bool need = !photo_fresh_locked(hex, now) && !photo_miss_recent_locked(hex, now);
    unlock();
    return need;
}

bool model_photo_soft(const char *hex)
{
    if (!hex || !hex[0]) return false;
    lock();
    int64_t now = now_us();
    int i = find_photo_locked(hex);
    bool soft = i >= 0 && s_ph[i].valid && !s_ph[i].full && photo_fresh_locked(hex, now)
                && !photo_miss_recent_locked(hex, now);
    unlock();
    return soft;
}

void model_cache_counts(int *on_screen, int *route_ok, int *route_need, int *photo_ok, int *ready)
{
    lock();
    int64_t now = now_us();
    int n = s_count;
    int rr = 0, rn = 0, po = 0, both = 0;
    for (int i = 0; i < n; i++) {
        bool need_route = s_planes[i].flight[0] && !flight_is_placeholder(s_planes[i].flight);
        bool has_route = true;
        if (need_route) {
            rn++;
            has_route = route_fresh_locked(find_route_locked(s_planes[i].flight), now);
            if (has_route) rr++;
        }
        bool has_photo = photo_fresh_locked(s_planes[i].hex, now);
        if (has_photo) po++;
        if (has_route && has_photo) both++;
    }
    unlock();
    if (on_screen) *on_screen = n;
    if (route_ok) *route_ok = rr;
    if (route_need) *route_need = rn;
    if (photo_ok) *photo_ok = po;
    if (ready) *ready = both;
}

bool model_next_photo(char *hex, int hex_n, char *reg, int reg_n)
{
    if (hex && hex_n) hex[0] = 0;
    if (reg && reg_n) reg[0] = 0;
    lock();
    if (s_count <= 0) {
        unlock();
        return false;
    }
    int64_t now = now_us();
    for (int k = 0; k < s_count; k++) {
        int i = (s_photo_cursor + k) % s_count;
        const char *h = s_planes[i].hex;
        if (!h[0]) continue;
        if (photo_fresh_locked(h, now) || photo_miss_recent_locked(h, now)) continue;
        if (hex && hex_n) snprintf(hex, hex_n, "%s", h);
        if (reg && reg_n) snprintf(reg, reg_n, "%s", s_planes[i].reg);
        s_photo_cursor = (i + 1) % s_count;
        unlock();
        return true;
    }
    unlock();
    return false;
}

uint32_t model_photo(const uint16_t **rgb565, int *w, int *h, char *credit, int credit_n)
{
    lock();
    if (rgb565) *rgb565 = s_photo;
    if (w) *w = s_photo_w;
    if (h) *h = s_photo_h;
    if (credit && credit_n) snprintf(credit, credit_n, "%s", s_credit);
    uint32_t seq = s_photo_seq;
    unlock();
    return seq;
}
