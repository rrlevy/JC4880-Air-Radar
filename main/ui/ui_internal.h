/* Cabecalho privado da interface. So os .c de main/ui o incluem.
   O contrato publico esta em ui.h. */
#pragma once

#include "ui.h"

#include "airports.h"
#include "brazil.h"
#include "rivers.h"
#include "cfg.h"
#include "logos.h"
#include "model.h"
#include "net.h"
#include "stat.h"
#include "tz.h"
#include "wifi_app.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "board_p4.h"
#include "driver/usb_serial_jtag.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#define SCR_W 800
#define SCR_H 480
/* Painel preto mais alto que o circulo, para os botoes caberem inteiros embaixo. */
#define RADAR_X 8
#define RADAR_Y 56
#define RADAR_W 430
#define RADAR_H 380
/* Folga em cima e embaixo para N e S ficarem fora do anel. */
#define RADAR_R 154
#define RADAR_CX (RADAR_W / 2)
#define RADAR_CY (36 + RADAR_R)
#define ZOOM_W 65
#define ZOOM_H 33
#define COL_BG     0x121212
#define COL_CARD   0x1C1C1C
#define COL_ORANGE 0xFF7020
#define COL_MUTED  0xA8A8A8
#define COL_RING   0x5A3A24
#define COL_TEXT   0xFFFFFF
#define CARD_W 346
#define CARD_H 412
#define PHOTO_AREA_Y 174
#define PHOTO_AREA_H 210
#define COL_APT 0x8FCFC4
#define FILT_N 4
#define TAB_N 5
#define TAB_W 124

/* Gol #FF7020, tinta escura no laranja.
   LATAM #E81848, o coral da marca, com tinta branca.
   Azul #0057B8, o azul do cartao, com tinta branca.
   Generico: fosforo de radar CRT, verde #39FF14. Anel e cruz no mesmo verde.
   A tinta do generico e escura porque o verde e claro. */
typedef struct {
    uint32_t accent;
    uint32_t ink;
    uint32_t ring;
    uint32_t cross;
} theme_pal_t;

/* Paleta na ordem Gol, Latam, Azul, Generico. theme_accent() e o tema corrente. */
extern const theme_pal_t k_theme[CFG_THEME_COUNT];


/* O brilho de trabalho e o mesmo do boot em main.c. 0 apaga de verdade. */
#define SCREEN_DUTY 230

uint32_t theme_accent(void);
uint32_t theme_ink(void);
uint32_t theme_ring(void);
uint32_t theme_cross(void);
int range_index_for_nm(int nm);
void ui_request_screenshot(void);
void shot_task(void *arg);
void touch_cb(lv_indev_t *indev, lv_indev_data_t *data);
void style_screen(lv_obj_t *scr);
void refresh_card(void);
void apply_photo(void);
void radar_draw(lv_event_t *e);
void radar_click(lv_event_t *e);
void rebuild_wifi_list(void);
void consume_prompt(void);
void open_wifi_screen(void);
void open_wifi(lv_event_t *e);
void close_wifi(lv_event_t *e);
void on_gesture(lv_event_t *e);
void bars_draw(lv_event_t *e);
void refresh_wifi_chip(void);
void apply_layer_checks(void);
void apply_kind_checks(void);
lv_obj_t *make_button(lv_obj_t *parent, const char *text);
void make_stat(lv_obj_t *parent, int index, int x, int y, int w, int h, const char *cap);
lv_obj_t *make_route_col(lv_obj_t *parent);
void on_zoom(lv_event_t *e);
lv_obj_t *make_zoom_btn(lv_obj_t *parent, int x, int y, int dir);
void apply_theme(void);
void show_tab(int tab);
void build_wifi(void);
void debug_build(void);
void debug_open(void);
void debug_refresh(void);
bool debug_is_open(void);

extern lv_obj_t *scr_radar;
extern lv_obj_t *scr_wifi;
extern lv_obj_t *radar;
extern lv_obj_t *lbl_brand;
extern lv_obj_t *img_logo;
extern lv_obj_t *lbl_flight;
extern lv_obj_t *lbl_meta;
extern lv_obj_t *lbl_from;
extern lv_obj_t *lbl_to;
extern lv_obj_t *route_col;
extern lv_obj_t *stat_box[4];
extern lv_obj_t *stat_val[4];
extern lv_obj_t *stat_unit[4];
extern lv_obj_t *lbl_foot;
extern lv_obj_t *lbl_count;
extern lv_obj_t *lbl_tz;
extern lv_obj_t *lbl_sp;
extern lv_obj_t *lbl_z;
extern lv_obj_t *lbl_title;
extern lv_obj_t *img_photo;
extern lv_obj_t *lbl_credit;
extern lv_obj_t *wifi_list;
extern lv_obj_t *pass_box;
extern lv_obj_t *ta_pass;
extern lv_obj_t *lbl_chosen;
extern lv_obj_t *lbl_wifi_status;
extern plane_t *s_view;
extern int s_view_n;
extern int s_view_sel;
extern float s_sweep;
extern bool s_sweep_ready;
extern uint32_t s_seq_seen;
extern uint32_t s_photo_seen;
extern bool s_wifi_open_once;
extern bool s_return_when_connected;
extern wifi_ap_t s_ui_aps[WIFI_AP_MAX];
extern int s_ui_ap_n;
extern uint32_t s_scan_seen;
extern char s_chosen[33];
extern lv_image_dsc_t s_photo_dsc;
extern lv_obj_t *lbl_wifi_name;
extern lv_obj_t *wifi_bars;
extern char s_chip_text[33];
extern int s_chip_bars;
extern bool s_press_from_top;
extern int s_press_y;
extern int s_swipe_x0;
extern int s_swipe_y0;
extern bool s_close_armed;
extern bool s_close_req;
extern int64_t s_commit_cycle;
extern bool s_pinching;
extern int s_pinch_d0;
extern int s_pinch_idx;
extern uint32_t s_prompt_seen;
extern lv_obj_t *tab_btn[TAB_N];
extern lv_obj_t *panel_coord;
extern lv_obj_t *panel_filt;
extern lv_obj_t *panel_geral;
extern lv_obj_t *panel_tema;
extern lv_obj_t *theme_tile[CFG_THEME_COUNT];
extern lv_obj_t *hdr_bar;
extern lv_obj_t *img_mark;
extern lv_obj_t *s_btns[24];
extern int s_btn_n;
extern lv_obj_t *s_tas[12];
extern int s_ta_n;
extern lv_obj_t *ta_lat;
extern lv_obj_t *ta_lon;
extern lv_obj_t *ta_iata;
extern lv_obj_t *ta_period;
extern lv_obj_t *ta_title;
extern lv_obj_t *ta_hdg;
extern lv_obj_t *lbl_coord_st;
extern lv_obj_t *cb_filt[FILT_N];
extern lv_obj_t *cb_map;
extern lv_obj_t *cb_rivers;
extern lv_obj_t *kb_shared;
extern int s_tab;
extern uint16_t *s_shot_buf;
extern volatile bool s_shot_req;
extern volatile bool s_shot_ready;
extern int s_shot_chunk;
extern volatile int s_nav;
extern volatile int s_nav_arg;
extern bool s_screen_on;
extern int64_t s_off_cycle;
extern bool s_hold_dropped;
