/* Fuso no centro do radar. A grade aponta uma regra POSIX; o calculo esta em tz_rule. */
#include "tz.h"

#include "tz_rule.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

/* Grade de 0,5 grau. Cada celula aponta uma regra POSIX (horario de verao incluso).
   Indice 0 nao tem regra: o fuso nautico, uma hora a cada 15 graus de longitude. */

extern const uint8_t tz_grid_bin_start[] asm("_binary_tz_grid_bin_start");
extern const uint8_t tz_grid_bin_end[] asm("_binary_tz_grid_bin_end");

#define TZ_ZONE_MAX 512

/* A tabela fica na PSRAM. Na RAM interna estes 2,5 KB derrubam a tarefa idle. */
static const char **s_zone;
static uint8_t *s_zlen;
static const uint8_t *s_cells;
static int s_nzone;
static int s_ready;
static int s_tried;

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static void bind(void)
{
    const uint8_t *p = tz_grid_bin_start;
    const uint8_t *end = tz_grid_bin_end;
    if (s_tried) return;
    s_tried = 1;
    if ((size_t)(end - p) < 12) return;
    if (memcmp(p, "TZG1", 4) != 0) return;
    int nlat = rd16(p + 4);
    int nlon = rd16(p + 6);
    int nzone = rd16(p + 8);
    if (nlat != 360 || nlon != 720 || nzone < 1 || nzone > TZ_ZONE_MAX) return;
    s_zone = heap_caps_malloc((size_t)nzone * sizeof(*s_zone), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_zlen = heap_caps_malloc((size_t)nzone, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_zone || !s_zlen) {
        heap_caps_free(s_zone);
        heap_caps_free(s_zlen);
        s_zone = NULL;
        s_zlen = NULL;
        ESP_LOGE("tz", "sem PSRAM para a tabela de fusos");
        return;
    }
    p += 12;
    for (int i = 0; i < nzone; i++) {
        if (p >= end) return;
        int len = *p++;
        if (p + len > end) return;
        s_zone[i] = (const char *)p;
        s_zlen[i] = (uint8_t)len;
        p += len;
    }
    size_t cells = (size_t)nlat * (size_t)nlon * sizeof(uint16_t);
    if ((size_t)(end - p) < cells) return;
    s_cells = p;
    s_nzone = nzone;
    s_ready = 1;
}

static int nautical_east(double lon)
{
    double h = (lon + 7.5) / 15.0;
    int hours = (int)h;
    if (h < 0 && h != (double)hours) hours--;
    if (hours > 14) hours = 14;
    if (hours < -12) hours = -12;
    return hours * 3600;
}

int tz_east_seconds(double lat, double lon, time_t now)
{
    if (!s_ready) bind();
    if (lat < -90.0) lat = -90.0;
    if (lat > 90.0) lat = 90.0;
    if (lon < -180.0) lon = -180.0;
    if (lon > 180.0) lon = 180.0;
    if (!s_ready) return nautical_east(lon);

    int iy = (int)((lat + 90.0) * 2.0);
    int ix = (int)((lon + 180.0) * 2.0);
    if (iy < 0) iy = 0;
    if (iy > 359) iy = 359;
    if (ix < 0) ix = 0;
    if (ix > 719) ix = 719;
    const uint8_t *cell = s_cells + ((size_t)iy * 720u + (size_t)ix) * 2u;
    uint16_t id = rd16(cell);
    if (id == 0 || id >= s_nzone || s_zlen[id] == 0) return nautical_east(lon);

    char rule[64];
    int n = s_zlen[id];
    if (n > (int)sizeof(rule) - 1) n = (int)sizeof(rule) - 1;
    memcpy(rule, s_zone[id], (size_t)n);
    rule[n] = 0;
    return tz_posix_east(rule, (int64_t)now);
}

void tz_format_label(char *buf, size_t n, int east_sec)
{
    if (!buf || n == 0) return;
    int neg = east_sec < 0;
    int a = neg ? -east_sec : east_sec;
    int h = a / 3600;
    int m = (a % 3600) / 60;
    int s = a % 60;
    char sign = neg ? '-' : '+';
    if (s != 0) snprintf(buf, n, "UTC%c%d:%02d:%02d", sign, h, m, s);
    else if (m != 0) snprintf(buf, n, "UTC%c%d:%02d", sign, h, m);
    else snprintf(buf, n, "UTC%c%d", sign, h);
}
