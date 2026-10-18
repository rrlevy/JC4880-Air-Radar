/* Marcas das companhias e dos temas, embutidas como RGB565A8.
   O nome do simbolo segue o arquivo (ver main/CMakeLists.txt, EMBED_FILES). */
#include "logos.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * Marcas em RGB565A8. Cada blob comeca com w, h, stride em uint16 little-endian,
 * depois o plano RGB565 e o plano de alfa (stride/2 por linha).
 * GOL: marca 2015. No fundo escuro o elo do O e RGB 252,246,240 a 25%.
 * azul_hdr e a mesma marca de azul.bin, sem o retangulo branco, para o fundo escuro.
 * As demais vieram de arquivos publicos (Wikimedia Commons ou o site do grupo).
 * VoePass saiu: a empresa nao opera mais e a marca nao e mais usada.
 * Sideral continua sem imagem; a UI mostra o codigo do voo.
 */

#define DECL(name) extern const uint8_t name##_bin_start[] asm("_binary_" #name "_bin_start")

DECL(gol);
DECL(gol_hdr);
DECL(latam);
DECL(azul);
DECL(azul_hdr);
DECL(total);
DECL(avianca);
DECL(wamos);
DECL(tap);
DECL(aal);
DECL(ual);
DECL(dal);
DECL(afr);
DECL(klm);
DECL(dlh);
DECL(ibe);
DECL(baw);
DECL(uae);
DECL(qtr);
DECL(thy);
DECL(cmp);
DECL(arg);
DECL(aca);
DECL(aea);
DECL(ity);
DECL(swr);
DECL(eth);
DECL(amx);
DECL(bov);
DECL(sku);
DECL(jat);

#undef DECL

#define DSC(name) static lv_image_dsc_t s_##name
DSC(gol); DSC(gol_hdr); DSC(latam); DSC(azul); DSC(azul_hdr); DSC(total);
DSC(avianca); DSC(wamos);
DSC(tap); DSC(aal); DSC(ual); DSC(dal); DSC(afr); DSC(klm); DSC(dlh);
DSC(ibe); DSC(baw); DSC(uae); DSC(qtr); DSC(thy); DSC(cmp); DSC(arg);
DSC(aca); DSC(aea); DSC(ity); DSC(swr); DSC(eth); DSC(amx); DSC(bov);
DSC(sku); DSC(jat);
#undef DSC

static bool s_ready;

static void bind(lv_image_dsc_t *dsc, const uint8_t *bin)
{
    uint16_t w = (uint16_t)(bin[0] | (bin[1] << 8));
    uint16_t h = (uint16_t)(bin[2] | (bin[3] << 8));
    uint16_t stride = (uint16_t)(bin[4] | (bin[5] << 8));
    memset(dsc, 0, sizeof(*dsc));
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565A8;
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.stride = stride;
    dsc->data_size = (uint32_t)stride * h + (uint32_t)(stride / 2) * h;
    dsc->data = bin + 6;
}

void logos_init(void)
{
    if (s_ready) return;
#define B(name) bind(&s_##name, name##_bin_start)
    B(gol); B(gol_hdr); B(latam); B(azul); B(azul_hdr); B(total);
    B(avianca); B(wamos);
    B(tap); B(aal); B(ual); B(dal); B(afr); B(klm); B(dlh);
    B(ibe); B(baw); B(uae); B(qtr); B(thy); B(cmp); B(arg);
    B(aca); B(aea); B(ity); B(swr); B(eth); B(amx); B(bov);
    B(sku); B(jat);
#undef B
    s_ready = true;
}

const lv_image_dsc_t *logo_gol_header(void)
{
    logos_init();
    return &s_gol_hdr;
}

const lv_image_dsc_t *logo_latam_header(void)
{
    logos_init();
    return &s_latam;
}

const lv_image_dsc_t *logo_azul_header(void)
{
    logos_init();
    return &s_azul_hdr;
}

const lv_image_dsc_t *airline_logo(const char *icao)
{
    logos_init();
    if (!icao || !icao[0]) return NULL;
    if (strcmp(icao, "GLO") == 0) return &s_gol;
    if (strcmp(icao, "TAM") == 0 || strcmp(icao, "LAN") == 0 || strcmp(icao, "LPE") == 0 ||
        strcmp(icao, "LNE") == 0 || strcmp(icao, "LXP") == 0 || strcmp(icao, "LAP") == 0 ||
        strcmp(icao, "ARE") == 0 || strcmp(icao, "LCO") == 0 || strcmp(icao, "LTG") == 0 ||
        strcmp(icao, "LAE") == 0) {
        return &s_latam;
    }
    if (strcmp(icao, "AZU") == 0 || strcmp(icao, "ACN") == 0) return &s_azul;
    if (strcmp(icao, "TTL") == 0) return &s_total;
    if (strcmp(icao, "AVA") == 0 || strcmp(icao, "TAI") == 0 ||
        strcmp(icao, "LRC") == 0 || strcmp(icao, "GLG") == 0) {
        return &s_avianca;
    }
    if (strcmp(icao, "PLM") == 0) return &s_wamos;
    if (strcmp(icao, "TAP") == 0) return &s_tap;
    if (strcmp(icao, "AAL") == 0) return &s_aal;
    if (strcmp(icao, "UAL") == 0) return &s_ual;
    if (strcmp(icao, "DAL") == 0) return &s_dal;
    if (strcmp(icao, "AFR") == 0) return &s_afr;
    if (strcmp(icao, "KLM") == 0) return &s_klm;
    if (strcmp(icao, "DLH") == 0) return &s_dlh;
    if (strcmp(icao, "IBE") == 0) return &s_ibe;
    if (strcmp(icao, "BAW") == 0) return &s_baw;
    if (strcmp(icao, "UAE") == 0) return &s_uae;
    if (strcmp(icao, "QTR") == 0) return &s_qtr;
    if (strcmp(icao, "THY") == 0) return &s_thy;
    if (strcmp(icao, "CMP") == 0) return &s_cmp;
    if (strcmp(icao, "ARG") == 0) return &s_arg;
    if (strcmp(icao, "ACA") == 0) return &s_aca;
    if (strcmp(icao, "AEA") == 0) return &s_aea;
    if (strcmp(icao, "ITY") == 0) return &s_ity;
    if (strcmp(icao, "SWR") == 0) return &s_swr;
    if (strcmp(icao, "ETH") == 0) return &s_eth;
    if (strcmp(icao, "AMX") == 0) return &s_amx;
    if (strcmp(icao, "BOV") == 0) return &s_bov;
    if (strcmp(icao, "SKU") == 0) return &s_sku;
    if (strcmp(icao, "JAT") == 0) return &s_jat;
    return NULL;
}
