#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PLANE_MAX 128
#define PHOTO_MAX_W 346
#define PHOTO_MAX_H 210

typedef struct {
    char hex[8];
    char flight[12];
    char reg[16];
    char type_code[10];
    char desc[48];
    char origin[40];
    char dest[40];
    char airline[16];
    char icao[4];
    uint32_t airline_color;
    float dist_nm;
    float bearing_deg;
    float track_deg;
    float east_nm;
    float north_nm;
    int alt_ft;
    int gs_kt;
    bool has_pos;
    bool on_ground;
    bool heli;
} plane_t;

void model_init(void);

void model_set_planes(const plane_t *list, int count);
/* Guarda a lista sem mostrar. A tela so troca quando a linha do radar passa na vertical. */
void model_stage_planes(const plane_t *list, int count);
bool model_commit_staged(void);
void model_clear(void);
void model_refilter(void);

/* Relogio comum da varredura e da consulta ADS-B. Angulo 0 e a vertical, para cima. */
void radar_sync_note_fetch_us(int64_t elapsed_us);
int64_t radar_sync_lead_us(void);
int64_t radar_sync_us_until_vertical(void);
float radar_sync_sweep_deg(void);
int radar_sync_age_s(void);
void radar_sync_align_slack(void);
bool radar_sync_aligned(void);
/* Numero do giro desde o epoch. Sobe quando a linha passa na vertical. */
int64_t radar_sync_cycle(void);
int model_copy(plane_t *out, int max, int *selected_index);

/* Lista curta para a captura: hex, ICAO, distancia em decimos de NM e foto no cache. */
typedef struct {
    char hex[8];
    char icao[4];
    int dist_x10;
    bool photo;
} model_pick_t;
int model_pick_copy(model_pick_t *out, int max);

void model_select_index(int index);
void model_select_hex(const char *hex);
/* Solta a aeronave escolhida e volta a seguir a mais proxima. */
void model_release_pin(void);
uint32_t model_seq(void);

void model_set_route(const char *flight, const char *origin, const char *dest);
void model_route_miss(const char *flight);
void model_route_fail(const char *flight);
void model_route_hold(const char *flight, int64_t us);
bool model_route_needed(const char *flight);
bool model_next_route(char *flight, int flight_n);

void model_mark_refresh(void);
int model_seconds_since_refresh(void);

bool model_selected_ids(char *flight, int flight_n, char *hex, int hex_n, char *reg, int reg_n);

#define MODEL_JOB_ROUTE 1
#define MODEL_JOB_PHOTO 2
#define MODEL_JOB_OP    3
/* Proxima aeronave mais proxima que ainda falta rota, foto ou dono. */
bool model_next_nearest(int *kind, char *flight, int flight_n, char *hex, int hex_n, char *reg, int reg_n);

typedef struct {
    char hex[8];
    char flight[12];
    char reg[16];
    char type_code[10];
    char desc[48];
    char origin[40];
    char dest[40];
    char airline[16];
    int alt_ft;
    int gs_kt;
    float dist_nm;
    float track_deg;
    int photo_w;
    int photo_h;
    bool photo_cached;
    bool photo_full;
} model_dbg_t;

bool model_debug_selected(model_dbg_t *out);

/* Guarda a foto na RAM (24 h) e mostra se essa aeronave estiver selecionada.
   full = false enquanto ainda existe uma miniatura maior para baixar. */
void model_photo_put(const char *hex, const uint16_t *rgb565, int w, int h, const char *credit, bool full);
void model_photo_finish(const char *hex);
void model_photo_miss(const char *hex);
void model_photo_fail(const char *hex);
void model_photo_hold(const char *hex, int64_t us);
bool model_photo_fresh(const char *hex);
bool model_photo_needed(const char *hex);
bool model_photo_soft(const char *hex);
/* Na lista atual: prontas tem rota (se houver voo) e foto no cache. */
void model_cache_counts(int *on_screen, int *route_ok, int *route_need, int *photo_ok, int *ready);
bool model_next_photo(char *hex, int hex_n, char *reg, int reg_n);
uint32_t model_photo(const uint16_t **rgb565, int *w, int *h, char *credit, int credit_n);

bool model_airline_allowed(const char *icao);
/* Cor do ponto no radar. Nao segue o tema da interface. */
uint32_t model_blip_color(const char *icao);

void geo_project_from(double lat0, double lon0, double lat, double lon,
                      float *dist_nm, float *bearing_deg, float *east_nm, float *north_nm);

void ascii_fold(char *s);
void trim_copy(char *dst, int dst_n, const char *src);
void airline_from_flight(const char *flight, char *icao, int icao_n,
                         char *name, int name_n, uint32_t *color);
/* Codigo ICAO de tres letras, como o da tabela interna. */
bool airline_from_icao(const char *icao, char *name, int name_n, uint32_t *color);
/* 00000000 e o indicativo vazio do transponder. Nao e um voo. */
bool flight_is_placeholder(const char *flight);

/* Dono da aeronave quando o indicativo nao traz a companhia. Chave: hex. */
void model_apply_operator(plane_t *p);
void model_set_operator(const char *hex, const char *icao, const char *name, uint32_t color);
void model_operator_miss(const char *hex);
bool model_operator_needed(const char *hex);
bool model_next_operator(char *hex, int hex_n, char *reg, int reg_n);
