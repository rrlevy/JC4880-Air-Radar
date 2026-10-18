#pragma once

#include <stdint.h>

/* Segundos a leste de UTC segundo uma string POSIX TZ, na data utc.
   String vazia devolve 0. */
int tz_posix_east(const char *posix, int64_t utc);
