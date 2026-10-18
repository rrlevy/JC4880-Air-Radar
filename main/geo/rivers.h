#pragma once

#include <stdint.h>

/* Polilinhas abertas. Coordenadas em graus * 400, pares (lat, lon). */
const int16_t *river_coords(void);
const uint16_t *river_runs(int *count);
