#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* Segundos a leste de UTC no ponto e no instante. Inclui horario de verao. */
int tz_east_seconds(double lat, double lon, time_t now);

/* "UTC-3", "UTC+5:30" ou "UTC+0". */
void tz_format_label(char *buf, size_t n, int east_sec);
