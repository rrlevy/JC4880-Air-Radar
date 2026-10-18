/* Amostras curtas para a tela de debug. Nao desenha nada. */
#include "stat.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define STAT_KEEP 16
#define TRACE_SLOTS 8

typedef struct {
    char hex[8];
    char flight[12];
    stat_step_t step[TRACE_N];
    int64_t used_us;
} ac_trace_t;

/* Fora da RAM interna: ela mal cabe a pilha da tarefa principal no arranque. */
static ac_trace_t *s_ac;
static int s_adsb_ms = -1;
static int s_adsb_res = -1;

typedef struct {
    int ms[STAT_KEEP];
    uint8_t res[STAT_KEEP];
    int next;
    int n;
} ring_t;

static SemaphoreHandle_t s_lock;
static ring_t s_http[STAT_KIND_N];
static char s_hex[8];
static bool s_opened;
static bool s_want_route;
static bool s_route_done;
static bool s_photo_done;
static int s_route_ms = -1;
static int s_photo_ms = -1;
static int64_t s_open_us;

static void ensure(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_ac) {
        s_ac = heap_caps_calloc(TRACE_SLOTS, sizeof(ac_trace_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_ac) {
            for (int i = 0; i < TRACE_SLOTS; i++) {
                for (int s = 0; s < TRACE_N; s++) {
                    s_ac[i].step[s].ms = -1;
                    s_ac[i].step[s].result = -1;
                }
            }
        }
    }
}

static void note_http(ring_t *r, int ms, int result)
{
    r->ms[r->next] = ms < 0 ? 0 : ms;
    r->res[r->next] = (uint8_t)result;
    r->next = (r->next + 1) % STAT_KEEP;
    if (r->n < STAT_KEEP) r->n++;
}

static void summarize(const ring_t *r, stat_http_t *out)
{
    int sum = 0, ok_n = 0, fails = 0, empty = 0, last = -1;
    if (r->n <= 0) {
        out->avg_ms = -1;
        out->last_ms = -1;
        out->n = 0;
        out->fails = 0;
        out->empty = 0;
        return;
    }
    int begin = (r->next - r->n + STAT_KEEP) % STAT_KEEP;
    for (int k = 0; k < r->n; k++) {
        int i = (begin + k) % STAT_KEEP;
        last = r->ms[i];
        if (r->res[i] == STAT_RES_FAIL) fails++;
        else if (r->res[i] == STAT_RES_EMPTY) empty++;
        else {
            sum += r->ms[i];
            ok_n++;
        }
    }
    out->avg_ms = ok_n > 0 ? sum / ok_n : -1;
    out->last_ms = last;
    out->n = r->n;
    out->fails = fails;
    out->empty = empty;
}

void stat_http(int kind, int ms, int result)
{
    if (kind < 0 || kind >= STAT_KIND_N) return;
    ensure();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    note_http(&s_http[kind], ms, result);
    xSemaphoreGive(s_lock);
}

static void step_clear(stat_step_t *s)
{
    s->ms = -1;
    s->result = -1;
    s->note[0] = 0;
}

static ac_trace_t *trace_slot(const char *hex, const char *flight, bool create)
{
    if (!s_ac) return NULL;
    ac_trace_t *by_hex = NULL;
    ac_trace_t *by_flight = NULL;
    ac_trace_t *empty = NULL;
    ac_trace_t *oldest = NULL;
    for (int i = 0; i < TRACE_SLOTS; i++) {
        ac_trace_t *t = &s_ac[i];
        if (!t->hex[0] && !t->flight[0]) {
            if (!empty) empty = t;
            continue;
        }
        if (hex && hex[0] && strcmp(t->hex, hex) == 0) by_hex = t;
        if (flight && flight[0] && strcmp(t->flight, flight) == 0) by_flight = t;
        if (!oldest || t->used_us < oldest->used_us) oldest = t;
    }
    ac_trace_t *t = by_hex ? by_hex : by_flight;
    if (!t && create) t = empty ? empty : oldest;
    if (!t) return NULL;
    if (create && t != by_hex && t != by_flight) {
        memset(t, 0, sizeof(*t));
        for (int i = 0; i < TRACE_N; i++) step_clear(&t->step[i]);
    }
    if (hex && hex[0] && !t->hex[0]) snprintf(t->hex, sizeof(t->hex), "%s", hex);
    if (flight && flight[0] && !t->flight[0]) snprintf(t->flight, sizeof(t->flight), "%s", flight);
    t->used_us = esp_timer_get_time();
    return t;
}

void stat_adsb_mark(int ms, int result)
{
    ensure();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_adsb_ms = ms < 0 ? 0 : ms;
    s_adsb_res = result;
    xSemaphoreGive(s_lock);
}

void stat_trace_mark(const char *hex, const char *flight, int step, int ms, int result, const char *note)
{
    if (step < 0 || step >= TRACE_N) return;
    ensure();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    ac_trace_t *t = trace_slot(hex, flight, true);
    if (t) {
        t->step[step].ms = ms < 0 ? 0 : ms;
        t->step[step].result = result;
        snprintf(t->step[step].note, sizeof(t->step[step].note), "%s", note ? note : "");
    }
    xSemaphoreGive(s_lock);
}

void stat_trace_copy(const char *hex, const char *flight, stat_trace_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->adsb_ms = -1;
    out->adsb_res = -1;
    for (int i = 0; i < TRACE_N; i++) step_clear(&out->step[i]);
    ensure();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    out->adsb_ms = s_adsb_ms;
    out->adsb_res = s_adsb_res;
    if (!s_ac) {
        xSemaphoreGive(s_lock);
        return;
    }
    for (int i = 0; i < TRACE_SLOTS; i++) {
        ac_trace_t *t = &s_ac[i];
        bool hex_hit = hex && hex[0] && strcmp(t->hex, hex) == 0;
        bool flight_hit = flight && flight[0] && strcmp(t->flight, flight) == 0;
        if (!hex_hit && !flight_hit) continue;
        for (int s = 0; s < TRACE_N; s++) {
            if (t->step[s].result < 0) continue;
            if (!hex_hit && s != TRACE_ROUTE) continue;
            out->step[s] = t->step[s];
        }
    }
    xSemaphoreGive(s_lock);
}

void stat_plane_opened(const char *hex, bool want_route)
{
    ensure();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_hex, sizeof(s_hex), "%s", hex ? hex : "");
    s_opened = s_hex[0] != 0;
    s_want_route = want_route;
    s_route_done = !want_route;
    s_photo_done = false;
    s_route_ms = want_route ? -1 : 0;
    s_photo_ms = -1;
    s_open_us = esp_timer_get_time();
    xSemaphoreGive(s_lock);
}

void stat_route_visible(const char *hex)
{
    if (!hex || !hex[0]) return;
    ensure();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_opened && !s_route_done && strcmp(s_hex, hex) == 0) {
        int64_t dt = esp_timer_get_time() - s_open_us;
        if (dt < 0) dt = 0;
        s_route_ms = (int)(dt / 1000);
        s_route_done = true;
    }
    xSemaphoreGive(s_lock);
}

void stat_photo_visible(const char *hex)
{
    if (!hex || !hex[0]) return;
    ensure();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_opened && !s_photo_done && strcmp(s_hex, hex) == 0) {
        int64_t dt = esp_timer_get_time() - s_open_us;
        if (dt < 0) dt = 0;
        s_photo_ms = (int)(dt / 1000);
        s_photo_done = true;
    }
    xSemaphoreGive(s_lock);
}

void stat_copy(stat_view_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->route_ms = -1;
    out->photo_ms = -1;
    ensure();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < STAT_KIND_N; i++) summarize(&s_http[i], &out->http[i]);
    memcpy(out->hex, s_hex, sizeof(out->hex));
    out->opened = s_opened;
    out->want_route = s_want_route;
    out->route_pending = s_opened && s_want_route && !s_route_done;
    out->photo_pending = s_opened && !s_photo_done;
    out->route_ms = s_route_ms;
    out->photo_ms = s_photo_ms;
    if (s_opened && (out->route_pending || out->photo_pending)) {
        int64_t dt = esp_timer_get_time() - s_open_us;
        if (dt < 0) dt = 0;
        out->wait_ms = (int)(dt / 1000);
    }
    xSemaphoreGive(s_lock);
}
