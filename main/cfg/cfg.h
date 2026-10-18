/* Configuracao persistente (namespace NVS "cfg").
   Chaves: lat, lon, rng, flt, per, hdg, thm, map, rio, ttl, knd.
   Nao renumerar os bits de filtro: a NVS de placas ja gravadas depende deles.
   O bit 0x10 era o grupo Abra e fica aposentado.
   knd e o tipo (aviao / helicoptero) e nao reaproveita bit de flt.
   Tema ausente ou invalido volta para o Generico. Mapa e rios nascem ligados.
   Sem a chave knd, aviao nasce ligado e helicoptero desligado. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CFG_FILT_GOL    0x01
#define CFG_FILT_LATAM  0x02
#define CFG_FILT_AZUL   0x04
#define CFG_FILT_OUTROS 0x08
#define CFG_FILT_ALL    0x0F

/* Chave NVS "knd". Nao mistura com flt. */
#define CFG_KIND_PLANE  0x01
#define CFG_KIND_HELI   0x02
#define CFG_KIND_DEFAULT CFG_KIND_PLANE

#define CFG_THEME_GOL      0
#define CFG_THEME_LATAM    1
#define CFG_THEME_AZUL     2
#define CFG_THEME_GENERIC  3
#define CFG_THEME_COUNT    4

void cfg_init(void);

double cfg_lat(void);
double cfg_lon(void);
float cfg_range_nm(void);
int cfg_range_index(void);
int cfg_period_s(void);
int cfg_heading_deg(void);
uint8_t cfg_filters(void);
/* Sobe quando o centro muda. Uma consulta antiga deve ser descartada. */
uint32_t cfg_gen(void);

void cfg_set_center(double lat, double lon);
void cfg_reset_center(void);
/* true se o IATA esta na tabela embutida. */
bool cfg_set_iata(const char *iata);
void cfg_set_period_s(int seconds);
/* Titulo ao lado da marca. No maximo 24 caracteres. Vazio volta para "Radar". */
#define CFG_TITLE_MAX 24
void cfg_title(char *dst, int n);
void cfg_set_title(const char *text);
/* 0 a 359. 360 vira 0. Norte no topo e 000; 090 coloca o leste no topo. */
void cfg_set_heading_deg(int degrees);
void cfg_set_filters(uint8_t bits);
uint8_t cfg_kinds(void);
void cfg_set_kinds(uint8_t bits);
/* Sobe quando um tipo e ligado. A consulta antiga, sem esse tipo, e descartada. */
uint32_t cfg_kind_gen(void);
void cfg_set_range_index(int index);
void cfg_zoom_in(void);
void cfg_zoom_out(void);
/* Sobe so quando o alcance aumenta. Uma consulta do raio antigo e descartada. */
uint32_t cfg_range_gen(void);
int cfg_theme(void);
void cfg_set_theme(int theme);
/* Mapa do Brasil e rios no radar. Ligados quando a chave ainda nao existe. */
bool cfg_show_map(void);
bool cfg_show_rivers(void);
void cfg_set_show_map(bool on);
void cfg_set_show_rivers(bool on);

float cfg_range_step(int index);
int cfg_range_step_count(void);
