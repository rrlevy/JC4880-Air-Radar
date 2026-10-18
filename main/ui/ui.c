/* Tela: estado compartilhado, tema, relogio, varredura e arranque.
   O desenho do radar, o cartao, o toque, o menu e a captura USB
   estao nos outros arquivos de main/ui. O estado mora aqui. */
#include "ui_internal.h"

static const char *TAG = "ui";

const theme_pal_t k_theme[CFG_THEME_COUNT] = {
    {0xFF7020, 0x1A1A1A, 0x5A3A24, 0x3A2A20},
    {0xE81848, 0xFFFFFF, 0x6A1830, 0x3C101C},
    {0x0057B8, 0xFFFFFF, 0x0E3F6E, 0x0A2C4C},
    {0x39FF14, 0x062006, 0x1C9A30, 0x146820},
};

static const theme_pal_t *theme_pal(void)
{
    int t = cfg_theme();
    if (t < 0 || t >= CFG_THEME_COUNT) t = CFG_THEME_GENERIC;
    return &k_theme[t];
}

uint32_t theme_accent(void) { return theme_pal()->accent; }
uint32_t theme_ink(void) { return theme_pal()->ink; }
uint32_t theme_ring(void) { return theme_pal()->ring; }
uint32_t theme_cross(void) { return theme_pal()->cross; }


lv_obj_t *scr_radar;
lv_obj_t *scr_wifi;
lv_obj_t *radar;
lv_obj_t *lbl_brand;
lv_obj_t *img_logo;
lv_obj_t *lbl_flight;
lv_obj_t *lbl_meta;
lv_obj_t *lbl_from;
lv_obj_t *lbl_to;
lv_obj_t *route_col;
lv_obj_t *stat_box[4];
lv_obj_t *stat_val[4];
lv_obj_t *stat_unit[4];
lv_obj_t *lbl_foot;
lv_obj_t *lbl_count;
lv_obj_t *lbl_tz;
lv_obj_t *lbl_sp;
lv_obj_t *lbl_z;
lv_obj_t *lbl_title;
lv_obj_t *img_photo;
lv_obj_t *lbl_credit;
lv_obj_t *wifi_list;
lv_obj_t *pass_box;
lv_obj_t *ta_pass;
lv_obj_t *lbl_chosen;
lv_obj_t *lbl_wifi_status;

plane_t *s_view;
int s_view_n;
int s_view_sel = -1;
float s_sweep;
bool s_sweep_ready;
uint32_t s_seq_seen;
uint32_t s_photo_seen;
bool s_wifi_open_once;
bool s_return_when_connected;
wifi_ap_t s_ui_aps[WIFI_AP_MAX];
int s_ui_ap_n;
uint32_t s_scan_seen;
char s_chosen[33];
lv_image_dsc_t s_photo_dsc;
lv_obj_t *lbl_wifi_name;
lv_obj_t *wifi_bars;
char s_chip_text[33];
int s_chip_bars = -1;
bool s_press_from_top;
int s_press_y = -1;
int s_swipe_x0;
int s_swipe_y0 = -1;
bool s_close_armed;
bool s_close_req;
int64_t s_commit_cycle = -1;
bool s_pinching;
int s_pinch_d0;
int s_pinch_idx;
uint32_t s_prompt_seen;
lv_obj_t *tab_btn[TAB_N];
lv_obj_t *panel_coord;
lv_obj_t *panel_filt;
lv_obj_t *panel_geral;
lv_obj_t *panel_tema;
lv_obj_t *theme_tile[CFG_THEME_COUNT];
lv_obj_t *hdr_bar;
lv_obj_t *img_mark;
lv_obj_t *s_btns[24];
int s_btn_n;
lv_obj_t *s_tas[12];
int s_ta_n;
lv_obj_t *ta_lat;
lv_obj_t *ta_lon;
lv_obj_t *ta_iata;
lv_obj_t *ta_period;
lv_obj_t *ta_title;
lv_obj_t *ta_hdg;
lv_obj_t *lbl_coord_st;
lv_obj_t *cb_filt[FILT_N];
lv_obj_t *cb_map;
lv_obj_t *cb_rivers;
lv_obj_t *kb_shared;
int s_tab;

static uint32_t tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

uint16_t *s_shot_buf;
volatile bool s_shot_req;
volatile bool s_shot_ready;
/* O anel de TX do USB Serial/JTAG recusa um bloco maior que o buffer.
   1024 cabe no buffer de 16 KB. Se o driver ja estava instalado, cai para 64. */
int s_shot_chunk = 64;
/* 1 volta ao radar, 2 abre a aba, 3 muda o alcance, 4 muda o tema,
   5 troca a tela, 6 liga o mapa, 7 liga os rios. */
volatile int s_nav;
volatile int s_nav_arg;
/* O brilho de trabalho e o mesmo do boot em main.c. 0 apaga de verdade. */
bool s_screen_on = true;
int64_t s_off_cycle = -1;
bool s_hold_dropped;

int range_index_for_nm(int nm)
{
    int n = cfg_range_step_count();
    int best = 0;
    float bestd = 100000.f;
    for (int i = 0; i < n; i++) {
        float d = cfg_range_step(i) - (float)nm;
        if (d < 0) d = -d;
        if (d < bestd) {
            bestd = d;
            best = i;
        }
    }
    return best;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    (void)area;
    const size_t bytes = (size_t)SCR_W * SCR_H * sizeof(uint16_t);
    esp_cache_msync(px, bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    if (s_shot_req && s_shot_buf) {
        memcpy(s_shot_buf, px, bytes);
        s_shot_req = false;
        s_shot_ready = true;
    }
    board_p4_present_rotated((const uint16_t *)px, SCR_W, SCR_H);
    lv_display_flush_ready(disp);
}

void style_screen(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(scr, &lv_font_montserrat_20, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}


static void refresh_clock(void);

static void screen_toggle(void)
{
    s_screen_on = !s_screen_on;
    if (!s_screen_on) {
        board_p4_set_brightness(0);
        net_set_dormant(true);
        s_off_cycle = radar_sync_cycle();
        s_hold_dropped = false;
        ESP_LOGI(TAG, "tela desligada");
        return;
    }
    net_set_dormant(false);
    board_p4_set_brightness(SCREEN_DUTY);
    if (s_hold_dropped) ESP_LOGI(TAG, "tela ligada, radar vazio");
    else ESP_LOGI(TAG, "tela ligada");
}

/* Dois giros completos com a tela apagada apagam os avioes. Antes disso, ficam. */
static void dormant_hold(int64_t cycle)
{
    if (s_screen_on || s_hold_dropped) return;
    if (cycle - s_off_cycle < 2) return;
    model_clear();
    s_hold_dropped = true;
    ESP_LOGI(TAG, "radar limpo apos 2 ciclos");
}

void apply_layer_checks(void)
{
    if (cb_map) {
        if (cfg_show_map()) lv_obj_add_state(cb_map, LV_STATE_CHECKED);
        else lv_obj_remove_state(cb_map, LV_STATE_CHECKED);
    }
    if (cb_rivers) {
        if (cfg_show_rivers()) lv_obj_add_state(cb_rivers, LV_STATE_CHECKED);
        else lv_obj_remove_state(cb_rivers, LV_STATE_CHECKED);
    }
}

static void anim_cb(lv_timer_t *timer)
{
    (void)timer;
    int nav = s_nav;
    if (nav) {
        int arg = s_nav_arg;
        s_nav = 0;
        if (nav == 1) {
            if (lv_screen_active() != scr_radar)
                lv_screen_load_anim(scr_radar, LV_SCR_LOAD_ANIM_OUT_TOP, 320, 0, false);
        } else if (nav == 2) {
            show_tab(arg);
            if (lv_screen_active() != scr_wifi) open_wifi_screen();
        } else if (nav == 3) {
            cfg_set_range_index(arg);
        } else if (nav == 4) {
            cfg_set_theme(arg);
            apply_theme();
        } else if (nav == 5) {
            screen_toggle();
        } else if (nav == 6) {
            cfg_set_show_map(arg != 0);
            apply_layer_checks();
        } else if (nav == 7) {
            cfg_set_show_rivers(arg != 0);
            apply_layer_checks();
        } else if (nav == 8 || nav == 9) {
            uint8_t k = cfg_kinds();
            uint8_t bit = nav == 8 ? CFG_KIND_PLANE : CFG_KIND_HELI;
            if (arg) k = (uint8_t)(k | bit);
            else k = (uint8_t)(k & (uint8_t)~bit);
            cfg_set_kinds(k);
            apply_kind_checks();
        }
    }
    if (s_shot_req) {
        lv_obj_t *scr = lv_screen_active();
        if (scr) lv_obj_invalidate(scr);
    }

    float deg = radar_sync_sweep_deg();
    int64_t cycle = radar_sync_cycle();
    dormant_hold(cycle);
    /* Uma vez por giro, no quadro em que a linha entra no topo. Vale com o menu aberto.
       Com a tela apagada a lista fica congelada ate ligar de novo, ou ate os 2 giros. */
    if (s_screen_on && s_sweep_ready && cycle != s_commit_cycle && deg < 80.f) {
        model_commit_staged();
        s_commit_cycle = cycle;
    }
    s_sweep = deg;
    s_sweep_ready = true;

    if (s_close_req) {
        s_close_req = false;
        close_wifi(NULL);
    }

    if (!s_wifi_open_once && wifi_hosted_ok()) {
        char ssid[33];
        wifi_ssid(ssid, sizeof(ssid));
        s_wifi_open_once = true;
        if (!ssid[0]) open_wifi_screen();
    }

    refresh_wifi_chip();

    if (lv_screen_active() == scr_radar) {
        uint32_t seq = model_seq();
        if (seq != s_seq_seen) {
            s_view_n = model_copy(s_view, PLANE_MAX, &s_view_sel);
            s_seq_seen = seq;
            refresh_card();
            apply_photo();
        } else {
            apply_photo();
        }
        int period = cfg_period_s();
        if (period < 2) period = 2;
        float sec = s_sweep * (float)period / 360.0f;
        if (sec < 0.f) sec = 0.f;
        if (sec >= (float)period) sec = 0.f;
        int sec_i = (int)sec;
        if (sec_i < 0) sec_i = 0;
        if (sec_i >= period) sec_i = 0;
        char age_txt[16];
        snprintf(age_txt, sizeof(age_txt), "%ds", sec_i);
        lv_label_set_text(lbl_foot, age_txt);
        refresh_clock();
        lv_obj_invalidate(radar);
    } else if (debug_is_open()) {
        debug_refresh();
    } else {
        char st[64];
        wifi_status(st, sizeof(st));
        lv_label_set_text(lbl_wifi_status, st);
        uint32_t scan = wifi_scan_seq();
        if (scan != s_scan_seen) {
            s_scan_seen = scan;
            rebuild_wifi_list();
        }
        consume_prompt();
        if (s_return_when_connected && wifi_is_connected()) {
            s_return_when_connected = false;
            lv_screen_load_anim(scr_radar, LV_SCR_LOAD_ANIM_OUT_TOP, 320, 0, false);
        }
    }
}

lv_obj_t *make_button(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_style_bg_color(btn, lv_color_hex(theme_accent()), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_t *lab = lv_label_create(btn);
    lv_label_set_text(lab, text);
    lv_obj_set_style_text_color(lab, lv_color_hex(theme_ink()), 0);
    lv_obj_center(lab);
    if (s_btn_n < (int)(sizeof(s_btns) / sizeof(s_btns[0]))) s_btns[s_btn_n++] = btn;
    return btn;
}

static void refresh_clock(void)
{
    char zone[20];
    char sp[20];
    char zu[20];
    time_t now = time(NULL);
    zone[0] = 0;
    if (now < 1700000000) {
        snprintf(sp, sizeof(sp), "--:--:--");
        snprintf(zu, sizeof(zu), "--:--:--Z");
    } else {
        struct tm utc;
        gmtime_r(&now, &utc);
        int east = tz_east_seconds(cfg_lat(), cfg_lon(), now);
        tz_format_label(zone, sizeof(zone), east);
        time_t local = now + (time_t)east;
        struct tm sp_tm;
        gmtime_r(&local, &sp_tm);
        snprintf(sp, sizeof(sp), "%02d:%02d:%02d", sp_tm.tm_hour, sp_tm.tm_min, sp_tm.tm_sec);
        snprintf(zu, sizeof(zu), "%02d:%02d:%02dZ", utc.tm_hour, utc.tm_min, utc.tm_sec);
    }
    static char shown_zone[20];
    static char shown_sp[20];
    static char shown_z[20];
    int zone_changed = strcmp(shown_zone, zone) != 0;
    if (zone_changed) {
        snprintf(shown_zone, sizeof(shown_zone), "%s", zone);
        if (lbl_tz) lv_label_set_text(lbl_tz, shown_zone);
        if (shown_zone[0]) ESP_LOGI(TAG, "fuso %s", shown_zone);
    }
    if (strcmp(shown_sp, sp) != 0 || zone_changed) {
        snprintf(shown_sp, sizeof(shown_sp), "%s", sp);
        if (lbl_sp) lv_label_set_text(lbl_sp, shown_sp);
        if (lbl_tz && lbl_sp) {
            lv_obj_update_layout(lbl_tz);
            int w = zone[0] ? lv_obj_get_width(lbl_tz) : 0;
            int gap = zone[0] ? 8 : 0;
            lv_obj_align(lbl_sp, LV_ALIGN_LEFT_MID, 12 + w + gap, 0);
        }
    }
    if (strcmp(shown_z, zu) != 0) {
        snprintf(shown_z, sizeof(shown_z), "%s", zu);
        if (lbl_z) lv_label_set_text(lbl_z, shown_z);
    }
}

/* 15x15. O traco do + e do - e o mesmo retangulo, para os dois ficarem iguais. */

static void build_radar(void)
{
    scr_radar = lv_obj_create(NULL);
    style_screen(scr_radar);

    hdr_bar = lv_obj_create(scr_radar);
    lv_obj_set_size(hdr_bar, SCR_W, 4);
    lv_obj_set_pos(hdr_bar, 0, 0);
    lv_obj_set_style_bg_color(hdr_bar, lv_color_hex(theme_accent()), 0);
    lv_obj_set_style_border_width(hdr_bar, 0, 0);
    lv_obj_set_style_radius(hdr_bar, 0, 0);
    lv_obj_clear_flag(hdr_bar, LV_OBJ_FLAG_SCROLLABLE);

    const lv_image_dsc_t *mark = logo_gol_header();
    img_mark = lv_image_create(scr_radar);
    lv_image_set_src(img_mark, mark);
    lv_obj_set_pos(img_mark, 14, 8);

    lbl_title = lv_label_create(scr_radar);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_20, 0);
    int title_x = 14 + (int)mark->header.w + 12;
    lv_obj_set_pos(lbl_title, title_x, 8 + ((int)mark->header.h - 22) / 2);
    int title_w = (SCR_W - 16 - 250 - 8) - title_x;
    if (title_w < 80) title_w = 80;
    lv_obj_set_width(lbl_title, title_w);
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_DOT);
    {
        char title[CFG_TITLE_MAX + 1];
        cfg_title(title, sizeof(title));
        lv_label_set_text(lbl_title, title);
    }

    lv_obj_t *wifi_chip = lv_obj_create(scr_radar);
    lv_obj_set_size(wifi_chip, 250, 40);
    lv_obj_set_pos(wifi_chip, SCR_W - 16 - 250, 6);
    lv_obj_set_style_bg_opa(wifi_chip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_chip, 0, 0);
    lv_obj_set_style_pad_all(wifi_chip, 0, 0);
    lv_obj_set_style_radius(wifi_chip, 0, 0);
    lv_obj_clear_flag(wifi_chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wifi_chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wifi_chip, open_wifi, LV_EVENT_CLICKED, NULL);

    wifi_bars = lv_obj_create(wifi_chip);
    lv_obj_set_size(wifi_bars, 34, 22);
    lv_obj_align(wifi_bars, LV_ALIGN_RIGHT_MID, 0, 2);
    lv_obj_set_style_bg_opa(wifi_bars, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_bars, 0, 0);
    lv_obj_set_style_pad_all(wifi_bars, 0, 0);
    lv_obj_clear_flag(wifi_bars, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(wifi_bars, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wifi_bars, bars_draw, LV_EVENT_DRAW_MAIN, NULL);

    lbl_wifi_name = lv_label_create(wifi_chip);
    lv_label_set_text(lbl_wifi_name, "Wi-Fi");
    lv_obj_set_width(lbl_wifi_name, 200);
    lv_label_set_long_mode(lbl_wifi_name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(lbl_wifi_name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_wifi_name, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_align(lbl_wifi_name, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(lbl_wifi_name, LV_ALIGN_RIGHT_MID, -40, 0);

    radar = lv_obj_create(scr_radar);
    lv_obj_set_pos(radar, RADAR_X, RADAR_Y);
    lv_obj_set_size(radar, RADAR_W, RADAR_H);
    lv_obj_set_style_bg_color(radar, lv_color_hex(0x0E0E0E), 0);
    lv_obj_set_style_bg_opa(radar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(radar, 0, 0);
    lv_obj_set_style_radius(radar, 12, 0);
    /* O tema card aplica 20 px de padding. O circulo e desenhado na borda do
       painel; sem zerar, os botoes nascem 20 px a direita e o da esquerda
       fica mais perto do anel. */
    lv_obj_set_style_pad_all(radar, 0, 0);
    lv_obj_clear_flag(radar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(radar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(radar, radar_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(radar, radar_click, LV_EVENT_CLICKED, NULL);

    lbl_count = lv_label_create(radar);
    lv_label_set_text(lbl_count, "00 ACFT");
    lv_obj_set_style_text_font(lbl_count, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_count, lv_color_hex(COL_MUTED), 0);
    /* 28,26 conserva o canto que o padding de 20 px ja mostrava (8+20, 6+20). */
    lv_obj_set_pos(lbl_count, 28, 26);
    lv_obj_clear_flag(lbl_count, LV_OBJ_FLAG_CLICKABLE);

    lbl_foot = lv_label_create(radar);
    lv_label_set_text(lbl_foot, "--s");
    lv_obj_set_style_text_font(lbl_foot, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_foot, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(lbl_foot, LV_ALIGN_TOP_RIGHT, -28, 26);
    lv_obj_clear_flag(lbl_foot, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *clocks = lv_obj_create(scr_radar);
    lv_obj_set_pos(clocks, RADAR_X, RADAR_Y + RADAR_H);
    lv_obj_set_size(clocks, RADAR_W, SCR_H - RADAR_Y - RADAR_H);
    lv_obj_set_style_bg_opa(clocks, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clocks, 0, 0);
    lv_obj_set_style_pad_all(clocks, 0, 0);
    lv_obj_set_style_radius(clocks, 0, 0);
    lv_obj_clear_flag(clocks, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(clocks, LV_OBJ_FLAG_CLICKABLE);

    lbl_tz = lv_label_create(clocks);
    lv_label_set_text(lbl_tz, "");
    lv_obj_set_style_text_font(lbl_tz, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_tz, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(lbl_tz, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_clear_flag(lbl_tz, LV_OBJ_FLAG_CLICKABLE);

    lbl_sp = lv_label_create(clocks);
    lv_label_set_text(lbl_sp, "--:--:--");
    lv_obj_set_style_text_font(lbl_sp, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_sp, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(lbl_sp, LV_ALIGN_LEFT_MID, 12, 0);

    lbl_z = lv_label_create(clocks);
    lv_label_set_text(lbl_z, "--:--:--Z");
    lv_obj_set_style_text_font(lbl_z, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_z, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(lbl_z, LV_ALIGN_RIGHT_MID, -12, 0);

    /* Espelho exato no centro do circulo. O canto interno fica 12 px fora do anel. */
    {
        const int rad = RADAR_R + 12;
        const int dx = (rad * 574 + 500) / 1000;
        const int dy = (rad * 819 + 500) / 1000;
        int y = RADAR_CY + dy;
        make_zoom_btn(radar, RADAR_CX - dx - ZOOM_W, y, -1);
        make_zoom_btn(radar, RADAR_CX + dx, y, 1);
    }

    lv_obj_t *card = lv_obj_create(scr_radar);
    lv_obj_set_pos(card, 446, 56);
    lv_obj_set_size(card, 346, 412);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    img_logo = lv_image_create(card);
    lv_obj_set_pos(img_logo, 16, 8);
    lv_obj_add_flag(img_logo, LV_OBJ_FLAG_HIDDEN);

    lbl_brand = lv_label_create(card);
    lv_obj_set_pos(lbl_brand, 16, 12);
    lv_obj_add_flag(lbl_brand, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(lbl_brand, &lv_font_montserrat_28, 0);
    lv_obj_set_style_radius(lbl_brand, 6, 0);
    lv_obj_set_style_pad_hor(lbl_brand, 8, 0);
    lv_obj_set_style_pad_ver(lbl_brand, 2, 0);
    lv_label_set_text(lbl_brand, "GOL");
    lv_obj_set_style_text_color(lbl_brand, lv_color_hex(COL_ORANGE), 0);

    lbl_flight = lv_label_create(card);
    lv_obj_set_pos(lbl_flight, 12, 12);
    lv_obj_set_width(lbl_flight, CARD_W - 24);
    lv_obj_set_style_text_font(lbl_flight, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(lbl_flight, lv_color_hex(COL_TEXT), 0);
    lv_label_set_long_mode(lbl_flight, LV_LABEL_LONG_DOT);
    lv_label_set_text(lbl_flight, "Aguardando");

    lbl_meta = lv_label_create(card);
    lv_obj_set_pos(lbl_meta, 12, 50);
    lv_obj_set_width(lbl_meta, CARD_W - 24);
    lv_obj_set_style_text_font(lbl_meta, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_meta, lv_color_hex(COL_MUTED), 0);
    lv_label_set_long_mode(lbl_meta, LV_LABEL_LONG_DOT);
    lv_label_set_text_fmt(lbl_meta, "Nenhum aviao no raio de %.0f NM", cfg_range_nm());

    route_col = make_route_col(card);
    lbl_from = lv_label_create(card);
    lv_obj_set_pos(lbl_from, 34, 70);
    lv_obj_set_width(lbl_from, CARD_W - 46);
    lv_obj_set_style_text_font(lbl_from, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_from, lv_color_hex(COL_TEXT), 0);
    lv_label_set_long_mode(lbl_from, LV_LABEL_LONG_DOT);
    lv_label_set_text(lbl_from, "");

    lbl_to = lv_label_create(card);
    lv_obj_set_pos(lbl_to, 34, 96);
    lv_obj_set_width(lbl_to, CARD_W - 46);
    lv_obj_set_style_text_font(lbl_to, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_to, lv_color_hex(COL_TEXT), 0);
    lv_label_set_long_mode(lbl_to, LV_LABEL_LONG_DOT);
    lv_label_set_text(lbl_to, "");

    make_stat(card, 0, 10, 120, 78, 46, "ALT");
    make_stat(card, 1, 94, 120, 78, 46, "GS");
    make_stat(card, 2, 178, 120, 78, 46, "TRK");
    make_stat(card, 3, 262, 120, 78, 46, "DIST");

    img_photo = lv_image_create(card);
    lv_obj_set_pos(img_photo, 0, PHOTO_AREA_Y);
    lv_obj_add_flag(img_photo, LV_OBJ_FLAG_HIDDEN);

    lbl_credit = lv_label_create(card);
    lv_obj_set_pos(lbl_credit, 8, CARD_H - 20);
    lv_obj_set_width(lbl_credit, CARD_W - 16);
    lv_obj_set_style_text_font(lbl_credit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_credit, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_style_text_align(lbl_credit, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_credit, LV_LABEL_LONG_DOT);
    lv_label_set_text(lbl_credit, "");
}

static void ui_task(void *arg)
{
    (void)arg;
    while (1) {
        uint32_t wait = lv_timer_handler();
        if (wait > 20) wait = 20;
        vTaskDelay(pdMS_TO_TICKS(wait ? wait : 5));
    }
}

void ui_start(void)
{
    s_view = heap_caps_calloc(PLANE_MAX, sizeof(plane_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_view) {
        ESP_LOGE(TAG, "sem memoria para o radar");
        return;
    }
    lv_init();
    lv_tick_set_cb(tick_ms);

    size_t bytes = (size_t)SCR_W * SCR_H * sizeof(uint16_t);
    void *buf = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "sem framebuffer");
        return;
    }

    lv_display_t *disp = lv_display_create(SCR_W, SCR_H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, buf, NULL, bytes, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, flush_cb);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_cb);
    lv_indev_add_event_cb(indev, on_gesture, LV_EVENT_GESTURE, NULL);

    logos_init();
    build_radar();
    build_wifi();
    refresh_card();
    lv_screen_load(scr_radar);
    lv_timer_create(anim_cb, 70, NULL);

    s_shot_buf = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    xTaskCreate(ui_task, "ui", 16384, NULL, 6, NULL);
    if (s_shot_buf) xTaskCreate(shot_task, "shot", 6144, NULL, 3, NULL);
    ESP_LOGI(TAG, "interface pronta, paisagem %dx%d", SCR_W, SCR_H);
}
