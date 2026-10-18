#pragma once

#include <stdint.h>

/* Classe da legenda OurAirports. O mapa deles revela mais aeroportos
   conforme o zoom aproxima. Nao e uma escala da ICAO.
   1 grande: linha aerea principal.
   2 medio: linha regional.
   3 local: pouco movimento regular.
   rwy_m e a pista mais longa, em metros, e so desempata dois da mesma
   classe que cairiam no mesmo ponto da tela. */
#define APT_RANK_LARGE  1
#define APT_RANK_MEDIUM 2
#define APT_RANK_SMALL  3

typedef struct {
    char iata[4];
    float lat;
    float lon;
    uint16_t rwy_m;
    uint8_t rank;
} apt_t;

/* Aeroportos brasileiros com servico regular e codigo IATA.
 * Coordenadas e classe: OurAirports (dominio publico). */
const apt_t *airports_all(int *count);
const apt_t *airport_by_iata(const char *code);
