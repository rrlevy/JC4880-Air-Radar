#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decodifica JPEG baseline ou progressivo e encaixa em max_w x max_h, sem cortar. */
bool jpgd_to_rgb565(const uint8_t *jpg, int jpg_len,
                    uint16_t *dst, int dst_bytes,
                    int max_w, int max_h,
                    int *out_w, int *out_h);

#ifdef __cplusplus
}
#endif
