#pragma once

#include "lvgl.h"

void logos_init(void);

/* Logo da companhia, ou NULL se nao houver imagem. */
const lv_image_dsc_t *airline_logo(const char *icao);

/* Wordmark do tema, para o topo da tela. Generico nao tem marca. */
const lv_image_dsc_t *logo_gol_header(void);
const lv_image_dsc_t *logo_latam_header(void);
const lv_image_dsc_t *logo_azul_header(void);
