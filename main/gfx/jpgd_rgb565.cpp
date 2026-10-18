/* JPEG da foto do aviao para RGB565, no tamanho do cartao. */
#include "jpgd_rgb565.h"

#include "jpgd.h"

#include <stdlib.h>

bool jpgd_to_rgb565(const uint8_t *jpg, int jpg_len,
                               uint16_t *dst, int dst_bytes,
                               int max_w, int max_h,
                               int *out_w, int *out_h)
{
    if (!jpg || jpg_len < 4 || !dst || !out_w || !out_h || max_w < 1 || max_h < 1) return false;

    int w = 0;
    int h = 0;
    int comps = 0;
    unsigned char *rgb = jpgd::decompress_jpeg_image_from_memory(
        jpg, jpg_len, &w, &h, &comps, 4);
    if (!rgb || w < 1 || h < 1) {
        free(rgb);
        return false;
    }

    if (max_w * max_h * (int)sizeof(uint16_t) > dst_bytes) {
        free(rgb);
        return false;
    }

    float sx = (float)max_w / (float)w;
    float sy = (float)max_h / (float)h;
    float s = sx < sy ? sx : sy;
    int dw = (int)((float)w * s + 0.5f);
    int dh = (int)((float)h * s + 0.5f);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    if (dw > max_w) dw = max_w;
    if (dh > max_h) dh = max_h;

    /* Mesmo retangulo para a miniatura e para a foto grande, senao o cartao pula. */
    const uint16_t bg = (uint16_t)(((0x1Cu & 0xF8u) << 8) | ((0x1Cu & 0xFCu) << 3) | (0x1Cu >> 3));
    int pixels = max_w * max_h;
    for (int i = 0; i < pixels; i++) dst[i] = bg;

    int ox = (max_w - dw) / 2;
    int oy = (max_h - dh) / 2;
    for (int y = 0; y < dh; y++) {
        int sy = (dh == 1) ? 0 : y * (h - 1) / (dh - 1);
        const unsigned char *row = rgb + (size_t)sy * (size_t)w * 4;
        uint16_t *out = dst + (oy + y) * max_w + ox;
        for (int x = 0; x < dw; x++) {
            int sx = (dw == 1) ? 0 : x * (w - 1) / (dw - 1);
            const unsigned char *p = row + (size_t)sx * 4;
            unsigned r = p[0];
            unsigned g = p[1];
            unsigned b = p[2];
            out[x] = (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
        }
    }

    free(rgb);
    *out_w = max_w;
    *out_h = max_h;
    return true;
}
