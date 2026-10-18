#pragma once

#include <stdbool.h>

/* Medias das ultimas respostas HTTP. A tela de debug e quem mostra. */
enum {
    STAT_ADSB = 0,
    STAT_ROUTE = 1,
    STAT_PHOTO = 2,
    STAT_OWNER = 3,
    STAT_KIND_N = 4
};

enum {
    STAT_RES_OK = 0,
    STAT_RES_FAIL = 1,
    STAT_RES_EMPTY = 2,
    STAT_RES_LATER = 3
};

typedef struct {
    int avg_ms;
    int last_ms;
    int n;
    int fails;
    int empty;
} stat_http_t;

typedef struct {
    stat_http_t http[STAT_KIND_N];
    char hex[8];
    bool opened;
    bool want_route;
    bool route_pending;
    bool photo_pending;
    int route_ms;
    int photo_ms;
    int wait_ms;
} stat_view_t;

enum {
    TRACE_ROUTE = 0,
    TRACE_PS_HEX,
    TRACE_PS_REG,
    TRACE_AIRPORT,
    TRACE_JPG_S,
    TRACE_DEC_S,
    TRACE_JPG_L,
    TRACE_DEC_L,
    TRACE_N
};

typedef struct {
    int ms;
    int result;
    char note[36];
} stat_step_t;

typedef struct {
    int adsb_ms;
    int adsb_res;
    stat_step_t step[TRACE_N];
} stat_trace_t;

void stat_http(int kind, int ms, int result);
void stat_adsb_mark(int ms, int result);
void stat_trace_mark(const char *hex, const char *flight, int step, int ms, int result, const char *note);
void stat_trace_copy(const char *hex, const char *flight, stat_trace_t *out);
void stat_plane_opened(const char *hex, bool want_route);
void stat_route_visible(const char *hex);
void stat_photo_visible(const char *hex);
void stat_copy(stat_view_t *out);
