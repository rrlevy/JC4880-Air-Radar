/* Configuracao: Wi-Fi, Coordenadas, Filtros, Geral e Tema.
   Abre pelo chip de Wi-Fi ou pelo gesto para baixo. */
#include "ui_internal.h"

static const char *const k_filt_name[FILT_N] = {
    "Gol", "Latam", "Azul", "Outros"
};
static const uint8_t k_filt_bit[FILT_N] = {
    CFG_FILT_GOL, CFG_FILT_LATAM, CFG_FILT_AZUL, CFG_FILT_OUTROS
};
static const char *const k_kind_name[2] = { "Avioes", "Helicopteros" };
static const uint8_t k_kind_bit[2] = { CFG_KIND_PLANE, CFG_KIND_HELI };
static lv_obj_t *cb_kind[2];
static bool s_kind_sync;

static void ap_chosen(lv_event_t *e);


static void style_ap_button(lv_obj_t *btn, bool current)
{
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1C1C1C), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(theme_accent()), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, lv_color_hex(current ? theme_accent() : COL_TEXT), 0);
    lv_obj_set_style_pad_left(btn, 12, 0);
    lv_obj_set_style_pad_right(btn, 10, 0);
    /* A lista herda Montserrat 20 e o botao do LVGL rola o texto em circulo.
       Sem isto o nome da rede sai pela esquerda. */
    uint32_t n = lv_obj_get_child_count(btn);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *ch = lv_obj_get_child(btn, (int32_t)i);
        if (!lv_obj_check_type(ch, &lv_label_class)) continue;
        lv_label_set_long_mode(ch, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(ch, &lv_font_montserrat_14, 0);
    }
}

void rebuild_wifi_list(void)
{
    char cur[33] = {0};
    bool connected = wifi_is_connected();
    if (connected) wifi_ssid(cur, sizeof(cur));

    s_ui_ap_n = wifi_copy_aps(s_ui_aps, WIFI_AP_MAX);
    lv_obj_clean(wifi_list);

    if (connected && cur[0]) {
        char line[80];
        int rssi = wifi_rssi();
        if (rssi > -127) {
            snprintf(line, sizeof(line), "%s    conectada    %d dBm", cur, rssi);
        } else {
            snprintf(line, sizeof(line), "%s    conectada", cur);
        }
        lv_obj_t *btn = lv_list_add_button(wifi_list, NULL, line);
        style_ap_button(btn, true);
    }

    int shown = 0;
    for (int i = 0; i < s_ui_ap_n; i++) {
        if (connected && cur[0] && strcmp(s_ui_aps[i].ssid, cur) == 0) continue;
        char line[80];
        const char *tag = "";
        if (s_ui_aps[i].open) tag = "   aberta";
        else if (wifi_has_saved(s_ui_aps[i].ssid)) tag = "   salva";
        snprintf(line, sizeof(line), "%s    %d dBm%s", s_ui_aps[i].ssid, s_ui_aps[i].rssi, tag);
        lv_obj_t *btn = lv_list_add_button(wifi_list, NULL, line);
        style_ap_button(btn, false);
        lv_obj_add_event_cb(btn, ap_chosen, LV_EVENT_CLICKED, (void *)(intptr_t)(i + 1));
        shown++;
    }
    if (!connected && shown == 0) {
        lv_obj_t *empty = lv_list_add_button(wifi_list, NULL, "Buscando redes");
        lv_obj_set_style_text_color(empty, lv_color_hex(COL_MUTED), 0);
    }
}

static void show_pass_box(const char *ssid)
{
    snprintf(s_chosen, sizeof(s_chosen), "%s", ssid ? ssid : "");
    lv_label_set_text_fmt(lbl_chosen, "Rede  %s", s_chosen);
    lv_textarea_set_text(ta_pass, "");
    lv_obj_remove_flag(pass_box, LV_OBJ_FLAG_HIDDEN);
}

static void ap_chosen(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e) - 1;
    if (idx < 0 || idx >= s_ui_ap_n) return;
    snprintf(s_chosen, sizeof(s_chosen), "%s", s_ui_aps[idx].ssid);
    lv_label_set_text_fmt(lbl_chosen, "Rede  %s", s_chosen);
    if (s_ui_aps[idx].open) {
        lv_obj_add_flag(pass_box, LV_OBJ_FLAG_HIDDEN);
        s_return_when_connected = true;
        wifi_connect_to(s_chosen, "");
        return;
    }
    if (wifi_connect_saved(s_chosen)) {
        lv_obj_add_flag(pass_box, LV_OBJ_FLAG_HIDDEN);
        s_return_when_connected = true;
        return;
    }
    show_pass_box(s_chosen);
}

void consume_prompt(void)
{
    uint32_t seq = wifi_prompt_seq();
    if (seq == s_prompt_seen) return;
    s_prompt_seen = seq;
    char ssid[33];
    wifi_prompt_ssid(ssid, sizeof(ssid));
    if (!ssid[0]) return;
    show_pass_box(ssid);
    s_return_when_connected = true;
}

void open_wifi_screen(void)
{
    if (lv_screen_active() == scr_wifi) {
        rebuild_wifi_list();
        wifi_request_scan();
        consume_prompt();
        return;
    }
    lv_obj_add_flag(pass_box, LV_OBJ_FLAG_HIDDEN);
    rebuild_wifi_list();
    /* OVER_BOTTOM: a tela nova entra de y negativo, descendo ate o lugar. */
    lv_screen_load_anim(scr_wifi, LV_SCR_LOAD_ANIM_OVER_BOTTOM, 380, 0, false);
    wifi_request_scan();
    consume_prompt();
}

void open_wifi(lv_event_t *e)
{
    (void)e;
    open_wifi_screen();
}

void close_wifi(lv_event_t *e)
{
    (void)e;
    if (lv_screen_active() == scr_radar) return;
    /* A tela de Wi-Fi sobe e revela o radar. */
    lv_screen_load_anim(scr_radar, LV_SCR_LOAD_ANIM_OUT_TOP, 320, 0, false);
}

static int bars_from_rssi(int rssi)
{
    if (rssi >= -55) return 4;
    if (rssi >= -65) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -85) return 1;
    return 0;
}

void bars_draw(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_coords(obj, &c);
    int base = c.y2 - 1;
    int x0 = c.x1 + 1;
    int level = s_chip_bars < 0 ? 0 : s_chip_bars;
    for (int i = 0; i < 4; i++) {
        int h = 5 + i * 4;
        int x = x0 + i * 8;
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = lv_color_hex(i < level ? COL_TEXT : 0x3A3A3A);
        d.bg_opa = LV_OPA_COVER;
        d.radius = 1;
        lv_area_t a = {x, base - h, x + 5, base};
        lv_draw_rect(layer, &d, &a);
    }
}

void refresh_wifi_chip(void)
{
    char ssid[33];
    wifi_ssid(ssid, sizeof(ssid));
    const char *show = ssid[0] ? ssid : "Wi-Fi";
    int bars = wifi_is_connected() ? bars_from_rssi(wifi_rssi()) : 0;
    if (strcmp(s_chip_text, show) != 0) {
        snprintf(s_chip_text, sizeof(s_chip_text), "%s", show);
        lv_label_set_text(lbl_wifi_name, s_chip_text);
    }
    if (bars != s_chip_bars) {
        s_chip_bars = bars;
        lv_obj_invalidate(wifi_bars);
    }
}

static void do_connect(lv_event_t *e)
{
    (void)e;
    if (!s_chosen[0]) return;
    const char *pass = lv_textarea_get_text(ta_pass);
    s_return_when_connected = true;
    wifi_connect_to(s_chosen, pass ? pass : "");
}

static void style_tab_btn(lv_obj_t *btn, bool on)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(on ? theme_accent() : 0x2A2A2A), 0);
    lv_obj_t *lab = lv_obj_get_child(btn, 0);
    if (lab) lv_obj_set_style_text_color(lab, lv_color_hex(on ? theme_ink() : COL_TEXT), 0);
}

static void layout_title(void)
{
    if (!img_mark || !lbl_title) return;
    const lv_image_dsc_t *mark = NULL;
    int th = cfg_theme();
    if (th == CFG_THEME_GOL) mark = logo_gol_header();
    else if (th == CFG_THEME_LATAM) mark = logo_latam_header();
    else if (th == CFG_THEME_AZUL) mark = logo_azul_header();
    if (!mark) {
        lv_obj_add_flag(img_mark, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(lbl_title, 14, 14);
        lv_obj_set_width(lbl_title, SCR_W - 16 - 250 - 28);
        return;
    }
    lv_image_set_src(img_mark, mark);
    lv_obj_remove_flag(img_mark, LV_OBJ_FLAG_HIDDEN);
    int src_h = (int)mark->header.h;
    int src_w = (int)mark->header.w;
    /* Wordmark larga fica em 30 px, na faixa do titulo. Marca quase quadrada
       pode subir ate 46 px, ainda acima do radar. */
    int fit = (src_w * 2 < src_h * 3) ? 46 : 30;
    int zoom = 256;
    if (src_h > fit) zoom = (fit * 256) / src_h;
    if (zoom < 32) zoom = 32;
    lv_image_set_pivot(img_mark, 0, 0);
    lv_image_set_scale(img_mark, zoom);
    int ih = (src_h * zoom) / 256;
    int iw = (src_w * zoom) / 256;
    if (ih < 1) ih = 1;
    int y = 4 + (36 - ih) / 2;
    if (y < 4) y = 4;
    lv_obj_set_pos(img_mark, 14, y);
    int title_x = 14 + iw + 10;
    int title_y = y + (ih - 22) / 2;
    if (title_y < 6) title_y = 6;
    lv_obj_set_pos(lbl_title, title_x, title_y);
    int title_w = (SCR_W - 16 - 250 - 8) - title_x;
    if (title_w < 48) title_w = 48;
    lv_obj_set_width(lbl_title, title_w);
}

void apply_theme(void)
{
    uint32_t accent = theme_accent();
    uint32_t ink = theme_ink();
    if (hdr_bar) lv_obj_set_style_bg_color(hdr_bar, lv_color_hex(accent), 0);
    layout_title();
    for (int i = 0; i < s_btn_n; i++) {
        if (!s_btns[i]) continue;
        lv_obj_set_style_bg_color(s_btns[i], lv_color_hex(accent), 0);
        lv_obj_t *lab = lv_obj_get_child(s_btns[i], 0);
        if (lab) lv_obj_set_style_text_color(lab, lv_color_hex(ink), 0);
    }
    for (int i = 0; i < TAB_N; i++) {
        if (tab_btn[i]) style_tab_btn(tab_btn[i], i == s_tab);
    }
    for (int i = 0; i < s_ta_n; i++) {
        if (s_tas[i]) lv_obj_set_style_border_color(s_tas[i], lv_color_hex(accent), 0);
    }
    if (ta_pass) lv_obj_set_style_border_color(ta_pass, lv_color_hex(accent), 0);
    for (int i = 0; i < FILT_N; i++) {
        if (!cb_filt[i]) continue;
        lv_obj_set_style_bg_color(cb_filt[i], lv_color_hex(accent), LV_PART_INDICATOR | LV_STATE_CHECKED);
    }
    for (int i = 0; i < 2; i++) {
        if (!cb_kind[i]) continue;
        lv_obj_set_style_bg_color(cb_kind[i], lv_color_hex(accent), LV_PART_INDICATOR | LV_STATE_CHECKED);
    }
    if (cb_map) lv_obj_set_style_bg_color(cb_map, lv_color_hex(accent), LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (cb_rivers) lv_obj_set_style_bg_color(cb_rivers, lv_color_hex(accent), LV_PART_INDICATOR | LV_STATE_CHECKED);
    for (int i = 0; i < 4; i++) {
        if (!stat_box[i]) continue;
        lv_obj_set_style_border_color(stat_box[i], lv_color_hex(accent), 0);
        lv_obj_t *cap = lv_obj_get_child(stat_box[i], 0);
        if (cap) lv_obj_set_style_text_color(cap, lv_color_hex(accent), 0);
    }
    for (int i = 0; i < CFG_THEME_COUNT; i++) {
        if (!theme_tile[i]) continue;
        bool on = i == cfg_theme();
        lv_obj_set_style_border_color(theme_tile[i], lv_color_hex(on ? k_theme[i].accent : 0x333333), 0);
        lv_obj_set_style_border_width(theme_tile[i], on ? 3 : 2, 0);
    }
    if (wifi_list && lv_screen_active() == scr_wifi && s_tab == 0) rebuild_wifi_list();
    if (route_col) lv_obj_invalidate(route_col);
    if (scr_radar) lv_obj_invalidate(scr_radar);
    if (scr_wifi) lv_obj_invalidate(scr_wifi);
}

static void fill_coord_fields(void)
{
    char b[24];
    if (ta_lat && ta_lon) {
        snprintf(b, sizeof(b), "%.6f", cfg_lat());
        lv_textarea_set_text(ta_lat, b);
        snprintf(b, sizeof(b), "%.6f", cfg_lon());
        lv_textarea_set_text(ta_lon, b);
    }
    if (ta_hdg) {
        snprintf(b, sizeof(b), "%03d", cfg_heading_deg());
        lv_textarea_set_text(ta_hdg, b);
    }
}

static void fill_period_field(void)
{
    if (!ta_period) return;
    char b[8];
    snprintf(b, sizeof(b), "%d", cfg_period_s());
    lv_textarea_set_text(ta_period, b);
}

static void fill_title_field(void)
{
    if (!ta_title) return;
    char b[CFG_TITLE_MAX + 1];
    cfg_title(b, sizeof(b));
    lv_textarea_set_text(ta_title, b);
    if (lbl_title) lv_label_set_text(lbl_title, b);
}

void show_tab(int tab)
{
    if (tab < 0 || tab >= TAB_N) tab = 0;
    s_tab = tab;
    for (int i = 0; i < TAB_N; i++) {
        if (tab_btn[i]) style_tab_btn(tab_btn[i], i == tab);
    }
    if (wifi_list) {
        if (tab == 0) lv_obj_remove_flag(wifi_list, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(wifi_list, LV_OBJ_FLAG_HIDDEN);
    }
    if (lbl_wifi_status) {
        if (tab == 0) lv_obj_remove_flag(lbl_wifi_status, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(lbl_wifi_status, LV_OBJ_FLAG_HIDDEN);
    }
    if (tab != 0 && pass_box) lv_obj_add_flag(pass_box, LV_OBJ_FLAG_HIDDEN);
    if (panel_coord) {
        if (tab == 1) lv_obj_remove_flag(panel_coord, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(panel_coord, LV_OBJ_FLAG_HIDDEN);
    }
    if (panel_filt) {
        if (tab == 2) lv_obj_remove_flag(panel_filt, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(panel_filt, LV_OBJ_FLAG_HIDDEN);
    }
    if (panel_geral) {
        if (tab == 3) lv_obj_remove_flag(panel_geral, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(panel_geral, LV_OBJ_FLAG_HIDDEN);
    }
    if (panel_tema) {
        if (tab == 4) lv_obj_remove_flag(panel_tema, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(panel_tema, LV_OBJ_FLAG_HIDDEN);
    }
    if (kb_shared) lv_obj_add_flag(kb_shared, LV_OBJ_FLAG_HIDDEN);
    if (tab == 1) fill_coord_fields();
    if (tab == 2) apply_kind_checks();
    if (tab == 3) {
        fill_period_field();
        fill_title_field();
    }
}

static void on_tab(lv_event_t *e)
{
    show_tab((int)(intptr_t)lv_event_get_user_data(e));
}

static bool parse_coord(const char *s, double *out)
{
    if (!s || !s[0] || !out) return false;
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s) return false;
    while (end && *end == ' ') end++;
    if (end && *end) return false;
    *out = v;
    return true;
}

static void on_save_center(lv_event_t *e)
{
    (void)e;
    double lat = 0, lon = 0;
    if (!parse_coord(lv_textarea_get_text(ta_lat), &lat) ||
        !parse_coord(lv_textarea_get_text(ta_lon), &lon) ||
        lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
        lv_label_set_text(lbl_coord_st, "Coordenada invalida");
        return;
    }
    cfg_set_center(lat, lon);
    lv_label_set_text(lbl_coord_st, "Centro salvo");
}

static void on_reset_center(lv_event_t *e)
{
    (void)e;
    cfg_reset_center();
    fill_coord_fields();
    lv_label_set_text(lbl_coord_st, "Centro padrao");
}

static void on_iata(lv_event_t *e)
{
    (void)e;
    const char *code = lv_textarea_get_text(ta_iata);
    if (!cfg_set_iata(code)) {
        lv_label_set_text(lbl_coord_st, "IATA desconhecido");
        return;
    }
    fill_coord_fields();
    lv_label_set_text(lbl_coord_st, "Centro no aeroporto");
}

static void on_filter(lv_event_t *e)
{
    (void)e;
    uint8_t mask = 0;
    for (int i = 0; i < FILT_N; i++) {
        if (cb_filt[i] && lv_obj_has_state(cb_filt[i], LV_STATE_CHECKED)) mask |= k_filt_bit[i];
    }
    cfg_set_filters(mask);
}

static void on_filter_row(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= FILT_N || !cb_filt[i]) return;
    if (lv_obj_has_state(cb_filt[i], LV_STATE_CHECKED))
        lv_obj_remove_state(cb_filt[i], LV_STATE_CHECKED);
    else
        lv_obj_add_state(cb_filt[i], LV_STATE_CHECKED);
    on_filter(e);
}

static void on_kind(lv_event_t *e)
{
    (void)e;
    if (s_kind_sync) return;
    uint8_t mask = 0;
    for (int i = 0; i < 2; i++) {
        if (cb_kind[i] && lv_obj_has_state(cb_kind[i], LV_STATE_CHECKED)) mask |= k_kind_bit[i];
    }
    cfg_set_kinds(mask);
}

static void on_kind_row(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i > 1 || !cb_kind[i]) return;
    if (lv_obj_has_state(cb_kind[i], LV_STATE_CHECKED))
        lv_obj_remove_state(cb_kind[i], LV_STATE_CHECKED);
    else
        lv_obj_add_state(cb_kind[i], LV_STATE_CHECKED);
    on_kind(e);
}

void apply_kind_checks(void)
{
    uint8_t k = cfg_kinds();
    s_kind_sync = true;
    for (int i = 0; i < 2; i++) {
        if (!cb_kind[i]) continue;
        if (k & k_kind_bit[i]) lv_obj_add_state(cb_kind[i], LV_STATE_CHECKED);
        else lv_obj_remove_state(cb_kind[i], LV_STATE_CHECKED);
    }
    s_kind_sync = false;
}

static void on_period_step(lv_event_t *e)
{
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    cfg_set_period_s(cfg_period_s() + dir);
    fill_period_field();
}

static void on_heading_apply(lv_event_t *e)
{
    (void)e;
    const char *s = ta_hdg ? lv_textarea_get_text(ta_hdg) : NULL;
    char *end = NULL;
    long v = (s && s[0]) ? strtol(s, &end, 10) : -1;
    if (!s || !s[0] || end == s || v < 0 || v > 360) {
        lv_label_set_text(lbl_coord_st, "Proa invalida");
        fill_coord_fields();
        return;
    }
    cfg_set_heading_deg((int)v);
    fill_coord_fields();
    lv_label_set_text_fmt(lbl_coord_st, "Proa %03d", cfg_heading_deg());
}

static void on_title_apply(lv_event_t *e)
{
    (void)e;
    cfg_set_title(ta_title ? lv_textarea_get_text(ta_title) : "");
    fill_title_field();
    if (kb_shared) lv_obj_add_flag(kb_shared, LV_OBJ_FLAG_HIDDEN);
}

static void on_period_apply(lv_event_t *e)
{
    (void)e;
    const char *s = lv_textarea_get_text(ta_period);
    char *end = NULL;
    long v = s ? strtol(s, &end, 10) : 0;
    if (!s || end == s) {
        fill_period_field();
        return;
    }
    cfg_set_period_s((int)v);
    fill_period_field();
}

static void on_debug(lv_event_t *e)
{
    (void)e;
    debug_open();
}

static void kb_hide(lv_event_t *e)
{
    (void)e;
    if (kb_shared) lv_obj_add_flag(kb_shared, LV_OBJ_FLAG_HIDDEN);
}

static void ta_focused(lv_event_t *e)
{
    if (!kb_shared) return;
    lv_keyboard_set_textarea(kb_shared, lv_event_get_target(e));
    lv_obj_remove_flag(kb_shared, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(kb_shared);
}

static void style_ta(lv_obj_t *ta)
{
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x111111), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(theme_accent()), 0);
    lv_obj_add_event_cb(ta, ta_focused, LV_EVENT_FOCUSED, NULL);
    if (s_ta_n < (int)(sizeof(s_tas) / sizeof(s_tas[0]))) s_tas[s_ta_n++] = ta;
}

static lv_obj_t *make_tab(lv_obj_t *parent, const char *text, int x, int id)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, 8);
    lv_obj_set_size(btn, TAB_W, 36);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2A2A), 0);
    lv_obj_t *lab = lv_label_create(btn);
    lv_label_set_text(lab, text);
    lv_obj_set_width(lab, TAB_W - 8);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(lab, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lab, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lab, lv_color_hex(COL_TEXT), 0);
    lv_obj_center(lab);
    lv_obj_add_event_cb(btn, on_tab, LV_EVENT_CLICKED, (void *)(intptr_t)id);
    return btn;
}

static lv_obj_t *plain_label(lv_obj_t *parent, const char *text, int x, int y, int w, bool muted)
{
    lv_obj_t *lab = lv_label_create(parent);
    lv_label_set_text(lab, text);
    lv_obj_set_pos(lab, x, y);
    if (w > 0) lv_obj_set_width(lab, w);
    lv_obj_set_style_text_font(lab, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lab, lv_color_hex(muted ? COL_MUTED : COL_TEXT), 0);
    return lab;
}

static void on_theme(lv_event_t *e)
{
    cfg_set_theme((int)(intptr_t)lv_event_get_user_data(e));
    apply_theme();
}

static void sync_layers_from_ui(void)
{
    cfg_set_show_map(cb_map && lv_obj_has_state(cb_map, LV_STATE_CHECKED));
    cfg_set_show_rivers(cb_rivers && lv_obj_has_state(cb_rivers, LV_STATE_CHECKED));
}

static void on_layer_box(lv_event_t *e)
{
    (void)e;
    sync_layers_from_ui();
}

static void on_layer_row(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_user_data(e);
    if (!cb) return;
    if (lv_obj_has_state(cb, LV_STATE_CHECKED))
        lv_obj_remove_state(cb, LV_STATE_CHECKED);
    else
        lv_obj_add_state(cb, LV_STATE_CHECKED);
    sync_layers_from_ui();
}

static lv_obj_t *make_layer_row(lv_obj_t *parent, const char *text, int y, bool on, lv_obj_t **out)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_pos(row, 408, y);
    lv_obj_set_size(row, 352, 52);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x2A2A2A), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *cb = lv_checkbox_create(row);
    *out = cb;
    lv_checkbox_set_text(cb, text);
    lv_obj_align(cb, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_text_font(cb, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(cb, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_bg_color(cb, lv_color_hex(theme_accent()), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(cb, lv_color_hex(COL_MUTED), LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(cb, 6, LV_PART_INDICATOR);
    lv_obj_set_ext_click_area(cb, 8);
    if (on) lv_obj_add_state(cb, LV_STATE_CHECKED);
    lv_obj_add_event_cb(cb, on_layer_box, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(row, on_layer_row, LV_EVENT_CLICKED, cb);
    return row;
}

void build_wifi(void)
{
    scr_wifi = lv_obj_create(NULL);
    style_screen(scr_wifi);

    static const char *const tab_name[TAB_N] = {
        "WiFi", "Coordenadas", "Filtros", "Geral", "Tema"
    };
    for (int i = 0; i < TAB_N; i++) {
        tab_btn[i] = make_tab(scr_wifi, tab_name[i], 8 + i * (TAB_W + 4), i);
    }

    lbl_wifi_status = lv_label_create(scr_wifi);
    lv_label_set_text(lbl_wifi_status, "");
    lv_obj_set_style_text_font(lbl_wifi_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_wifi_status, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_pos(lbl_wifi_status, 16, 52);
    lv_obj_set_width(lbl_wifi_status, 640);

    lv_obj_t *back = make_button(scr_wifi, "Voltar");
    lv_obj_set_pos(back, 656, 8);
    lv_obj_set_size(back, 132, 36);
    lv_obj_add_event_cb(back, close_wifi, LV_EVENT_CLICKED, NULL);

    wifi_list = lv_list_create(scr_wifi);
    lv_obj_set_pos(wifi_list, 12, 78);
    lv_obj_set_size(wifi_list, 360, 390);
    lv_obj_set_style_bg_color(wifi_list, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_border_width(wifi_list, 0, 0);
    lv_obj_set_style_radius(wifi_list, 8, 0);
    lv_obj_set_scroll_dir(wifi_list, LV_DIR_VER);

    pass_box = lv_obj_create(scr_wifi);
    lv_obj_set_pos(pass_box, 384, 78);
    lv_obj_set_size(pass_box, 404, 390);
    lv_obj_set_style_bg_color(pass_box, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_border_width(pass_box, 0, 0);
    lv_obj_set_style_radius(pass_box, 12, 0);
    lv_obj_set_style_pad_all(pass_box, 8, 0);
    lv_obj_clear_flag(pass_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pass_box, LV_OBJ_FLAG_HIDDEN);

    lbl_chosen = lv_label_create(pass_box);
    lv_label_set_text(lbl_chosen, "Rede");
    lv_obj_set_width(lbl_chosen, 380);
    lv_obj_set_style_text_font(lbl_chosen, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl_chosen, LV_ALIGN_TOP_LEFT, 4, 0);

    ta_pass = lv_textarea_create(pass_box);
    lv_obj_set_width(ta_pass, 380);
    lv_obj_align(lbl_chosen, LV_ALIGN_TOP_LEFT, 4, 0);
    lv_obj_align(ta_pass, LV_ALIGN_TOP_LEFT, 4, 36);
    lv_textarea_set_placeholder_text(ta_pass, "Senha");
    lv_textarea_set_max_length(ta_pass, 63);
    lv_textarea_set_one_line(ta_pass, true);
    lv_obj_set_style_bg_color(ta_pass, lv_color_hex(0x111111), 0);
    lv_obj_set_style_text_color(ta_pass, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_border_color(ta_pass, lv_color_hex(theme_accent()), 0);

    lv_obj_t *go = make_button(pass_box, "Conectar");
    lv_obj_set_size(go, 140, 36);
    lv_obj_align(go, LV_ALIGN_TOP_RIGHT, -4, 80);
    lv_obj_add_event_cb(go, do_connect, LV_EVENT_CLICKED, NULL);

    lv_obj_t *kb = lv_keyboard_create(pass_box);
    lv_obj_set_size(kb, 388, 200);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, ta_pass);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x161616), 0);
    lv_obj_add_event_cb(kb, do_connect, LV_EVENT_READY, NULL);

    panel_coord = lv_obj_create(scr_wifi);
    lv_obj_set_pos(panel_coord, 12, 78);
    lv_obj_set_size(panel_coord, 776, 390);
    lv_obj_set_style_bg_opa(panel_coord, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel_coord, 0, 0);
    lv_obj_set_style_pad_all(panel_coord, 0, 0);
    lv_obj_clear_flag(panel_coord, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel_coord, LV_OBJ_FLAG_HIDDEN);

    plain_label(panel_coord, "Latitude", 8, 0, 0, false);
    ta_lat = lv_textarea_create(panel_coord);
    lv_obj_set_pos(ta_lat, 8, 28);
    lv_obj_set_width(ta_lat, 320);
    lv_textarea_set_max_length(ta_lat, 16);
    lv_textarea_set_accepted_chars(ta_lat, "-0123456789.");
    style_ta(ta_lat);

    plain_label(panel_coord, "Longitude", 8, 84, 0, false);
    ta_lon = lv_textarea_create(panel_coord);
    lv_obj_set_pos(ta_lon, 8, 112);
    lv_obj_set_width(ta_lon, 320);
    lv_textarea_set_max_length(ta_lon, 16);
    lv_textarea_set_accepted_chars(ta_lon, "-0123456789.");
    style_ta(ta_lon);

    lv_obj_t *save_c = make_button(panel_coord, "Salvar");
    lv_obj_set_pos(save_c, 8, 168);
    lv_obj_set_size(save_c, 140, 40);
    lv_obj_add_event_cb(save_c, on_save_center, LV_EVENT_CLICKED, NULL);
    lv_obj_t *reset_c = make_button(panel_coord, "Padrao");
    lv_obj_set_pos(reset_c, 160, 168);
    lv_obj_set_size(reset_c, 140, 40);
    lv_obj_add_event_cb(reset_c, on_reset_center, LV_EVENT_CLICKED, NULL);

    plain_label(panel_coord, "Aeroporto IATA", 8, 228, 0, false);
    ta_iata = lv_textarea_create(panel_coord);
    lv_obj_set_pos(ta_iata, 8, 256);
    lv_obj_set_width(ta_iata, 140);
    lv_textarea_set_max_length(ta_iata, 3);
    lv_textarea_set_accepted_chars(ta_iata, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    style_ta(ta_iata);
    lv_obj_t *go_iata = make_button(panel_coord, "Ir");
    lv_obj_set_pos(go_iata, 160, 256);
    lv_obj_set_size(go_iata, 100, 40);
    lv_obj_add_event_cb(go_iata, on_iata, LV_EVENT_CLICKED, NULL);

    lbl_coord_st = plain_label(panel_coord, "", 8, 312, 380, true);

    plain_label(panel_coord, "Proa", 420, 0, 0, false);
    ta_hdg = lv_textarea_create(panel_coord);
    lv_obj_set_pos(ta_hdg, 420, 28);
    lv_obj_set_width(ta_hdg, 120);
    lv_textarea_set_max_length(ta_hdg, 3);
    lv_textarea_set_accepted_chars(ta_hdg, "0123456789");
    style_ta(ta_hdg);
    lv_obj_t *hdg_ok = make_button(panel_coord, "Aplicar");
    lv_obj_set_pos(hdg_ok, 420, 84);
    lv_obj_set_size(hdg_ok, 140, 40);
    lv_obj_add_event_cb(hdg_ok, on_heading_apply, LV_EVENT_CLICKED, NULL);
    plain_label(panel_coord, "000 Norte para cima.", 420, 140, 320, true);
    plain_label(panel_coord, "090 Leste para cima.", 420, 172, 320, true);

    plain_label(panel_coord, "Relevo", 420, 214, 0, false);
    make_layer_row(panel_coord, "Mapa do Brasil", 246, cfg_show_map(), &cb_map);
    make_layer_row(panel_coord, "Rios", 304, cfg_show_rivers(), &cb_rivers);

    panel_filt = lv_obj_create(scr_wifi);
    lv_obj_set_pos(panel_filt, 12, 78);
    lv_obj_set_size(panel_filt, 776, 390);
    lv_obj_set_style_bg_opa(panel_filt, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel_filt, 0, 0);
    lv_obj_set_style_pad_all(panel_filt, 0, 0);
    lv_obj_clear_flag(panel_filt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel_filt, LV_OBJ_FLAG_HIDDEN);

    plain_label(panel_filt, "Mostrar no radar", 8, 4, 0, false);
    uint8_t cur_filt = cfg_filters();
    for (int i = 0; i < FILT_N; i++) {
        lv_obj_t *row = lv_obj_create(panel_filt);
        lv_obj_set_pos(row, 0, 36 + i * 64);
        lv_obj_set_size(row, 368, 58);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x2A2A2A), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, on_filter_row, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *cb = lv_checkbox_create(row);
        cb_filt[i] = cb;
        lv_checkbox_set_text(cb, k_filt_name[i]);
        lv_obj_align(cb, LV_ALIGN_LEFT_MID, 16, 0);
        lv_obj_set_style_text_font(cb, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(cb, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_bg_color(cb, lv_color_hex(theme_accent()), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_border_color(cb, lv_color_hex(COL_MUTED), LV_PART_INDICATOR);
        lv_obj_set_style_pad_all(cb, 6, LV_PART_INDICATOR);
        lv_obj_set_ext_click_area(cb, 12);
        if (cur_filt & k_filt_bit[i]) lv_obj_add_state(cb, LV_STATE_CHECKED);
        lv_obj_add_event_cb(cb, on_filter, LV_EVENT_VALUE_CHANGED, NULL);
    }

    plain_label(panel_filt, "Tipo", 408, 4, 0, false);
    uint8_t cur_kind = cfg_kinds();
    for (int i = 0; i < 2; i++) {
        lv_obj_t *row = lv_obj_create(panel_filt);
        lv_obj_set_pos(row, 400, 36 + i * 64);
        lv_obj_set_size(row, 360, 58);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x2A2A2A), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, on_kind_row, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *cb = lv_checkbox_create(row);
        cb_kind[i] = cb;
        lv_checkbox_set_text(cb, k_kind_name[i]);
        lv_obj_align(cb, LV_ALIGN_LEFT_MID, 16, 0);
        lv_obj_set_style_text_font(cb, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(cb, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_bg_color(cb, lv_color_hex(theme_accent()), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_border_color(cb, lv_color_hex(COL_MUTED), LV_PART_INDICATOR);
        lv_obj_set_style_pad_all(cb, 6, LV_PART_INDICATOR);
        lv_obj_set_ext_click_area(cb, 12);
        if (cur_kind & k_kind_bit[i]) lv_obj_add_state(cb, LV_STATE_CHECKED);
        lv_obj_add_event_cb(cb, on_kind, LV_EVENT_VALUE_CHANGED, NULL);
    }

    panel_geral = lv_obj_create(scr_wifi);
    lv_obj_set_pos(panel_geral, 12, 78);
    lv_obj_set_size(panel_geral, 776, 390);
    lv_obj_set_style_bg_opa(panel_geral, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel_geral, 0, 0);
    lv_obj_set_style_pad_all(panel_geral, 0, 0);
    lv_obj_clear_flag(panel_geral, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel_geral, LV_OBJ_FLAG_HIDDEN);

    plain_label(panel_geral, "Atualizacao da API e do radar", 8, 4, 400, false);
    ta_period = lv_textarea_create(panel_geral);
    lv_obj_set_pos(ta_period, 8, 44);
    lv_obj_set_width(ta_period, 120);
    lv_textarea_set_max_length(ta_period, 3);
    lv_textarea_set_accepted_chars(ta_period, "0123456789");
    style_ta(ta_period);
    plain_label(panel_geral, "segundos", 140, 52, 0, true);

    lv_obj_t *per_minus = make_button(panel_geral, "-");
    lv_obj_set_pos(per_minus, 8, 108);
    lv_obj_set_size(per_minus, 72, 40);
    lv_obj_add_event_cb(per_minus, on_period_step, LV_EVENT_CLICKED, (void *)(intptr_t)(-1));
    lv_obj_t *per_plus = make_button(panel_geral, "+");
    lv_obj_set_pos(per_plus, 92, 108);
    lv_obj_set_size(per_plus, 72, 40);
    lv_obj_add_event_cb(per_plus, on_period_step, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_t *per_ok = make_button(panel_geral, "Aplicar");
    lv_obj_set_pos(per_ok, 180, 108);
    lv_obj_set_size(per_ok, 140, 40);
    lv_obj_add_event_cb(per_ok, on_period_apply, LV_EVENT_CLICKED, NULL);
    plain_label(panel_geral, "A varredura completa uma volta nesse tempo.", 8, 168, 400, true);
    plain_label(panel_geral, "De 2 a 120 segundos.", 8, 224, 400, true);

    plain_label(panel_geral, "Titulo", 430, 4, 0, false);
    ta_title = lv_textarea_create(panel_geral);
    lv_obj_set_pos(ta_title, 430, 44);
    lv_obj_set_width(ta_title, 300);
    lv_textarea_set_max_length(ta_title, CFG_TITLE_MAX);
    style_ta(ta_title);
    lv_obj_t *title_ok = make_button(panel_geral, "Aplicar");
    lv_obj_set_pos(title_ok, 430, 108);
    lv_obj_set_size(title_ok, 140, 40);
    lv_obj_add_event_cb(title_ok, on_title_apply, LV_EVENT_CLICKED, NULL);
    plain_label(panel_geral, "Ao lado da marca. Vazio volta para Radar.", 430, 168, 330, true);

    lv_obj_t *dbg = make_button(panel_geral, "Debug");
    lv_obj_set_pos(dbg, 8, 342);
    lv_obj_set_size(dbg, 110, 36);
    lv_obj_add_event_cb(dbg, on_debug, LV_EVENT_CLICKED, NULL);

    panel_tema = lv_obj_create(scr_wifi);
    lv_obj_set_pos(panel_tema, 12, 78);
    lv_obj_set_size(panel_tema, 776, 390);
    lv_obj_set_style_bg_opa(panel_tema, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel_tema, 0, 0);
    lv_obj_set_style_pad_all(panel_tema, 0, 0);
    lv_obj_clear_flag(panel_tema, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel_tema, LV_OBJ_FLAG_HIDDEN);

    static const char *const theme_name[CFG_THEME_COUNT] = {"Gol", "Latam", "Azul", "Generico"};
    const lv_image_dsc_t *marks[CFG_THEME_COUNT] = {
        logo_gol_header(), logo_latam_header(), logo_azul_header(), NULL
    };
    /* Um so tamanho de botao, o da maior marca em 1:1. Nenhuma e ampliada. */
    const int pad_x = 16;
    const int pad_top = 10;
    const int cap_gap = 6;
    const int cap_h = 16;
    const int pad_bot = 8;
    const int gap = 16;
    int nat_w[CFG_THEME_COUNT];
    int nat_h[CFG_THEME_COUNT];
    int max_w = 0;
    int max_h = 0;
    for (int i = 0; i < CFG_THEME_COUNT; i++) {
        if (marks[i]) {
            nat_w[i] = (int)marks[i]->header.w;
            nat_h[i] = (int)marks[i]->header.h;
        } else {
            lv_point_t sz;
            lv_text_get_size(&sz, "Generico", &lv_font_montserrat_28, 0, 0, LV_COORD_MAX, 0);
            nat_w[i] = sz.x;
            nat_h[i] = sz.y;
        }
        if (nat_w[i] > max_w) max_w = nat_w[i];
        if (nat_h[i] > max_h) max_h = nat_h[i];
    }
    const int bw = max_w + pad_x * 2;
    const int bh = pad_top + max_h + cap_gap + cap_h + pad_bot;
    int group_w = bw * 2 + gap;
    int group_h = bh * 2 + gap;
    /* set_size ainda nao atualizou a coordenada; usa o tamanho do painel. */
    int origin_x = (776 - group_w) / 2;
    int origin_y = (390 - group_h) / 2;
    if (origin_x < 8) origin_x = 8;
    if (origin_y < 8) origin_y = 8;
    for (int i = 0; i < CFG_THEME_COUNT; i++) {
        int col = i % 2;
        int row = i / 2;
        lv_obj_t *tile = lv_obj_create(panel_tema);
        theme_tile[i] = tile;
        lv_obj_set_pos(tile, origin_x + col * (bw + gap), origin_y + row * (bh + gap));
        lv_obj_set_size(tile, bw, bh);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x161616), 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(tile, 12, 0);
        lv_obj_set_style_border_color(tile, lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(tile, 2, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_set_style_shadow_width(tile, 0, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(tile, on_theme, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        if (marks[i]) {
            lv_obj_t *img = lv_image_create(tile);
            lv_image_set_src(img, marks[i]);
            lv_image_set_scale(img, 256);
            lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_pos(img, (bw - nat_w[i]) / 2, pad_top + (max_h - nat_h[i]) / 2);
        } else {
            lv_obj_t *word = lv_label_create(tile);
            lv_label_set_text(word, "Generico");
            lv_obj_set_style_text_font(word, &lv_font_montserrat_28, 0);
            lv_obj_set_style_text_color(word, lv_color_hex(k_theme[CFG_THEME_GENERIC].accent), 0);
            lv_obj_set_pos(word, (bw - nat_w[i]) / 2, pad_top + (max_h - nat_h[i]) / 2);
        }
        lv_obj_t *cap = lv_label_create(tile);
        lv_label_set_text(cap, marks[i] ? theme_name[i] : "Neutro");
        lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(cap, lv_color_hex(COL_MUTED), 0);
        lv_obj_align(cap, LV_ALIGN_BOTTOM_MID, 0, -pad_bot);
    }

    kb_shared = lv_keyboard_create(scr_wifi);
    lv_obj_set_size(kb_shared, 776, 210);
    lv_obj_align(kb_shared, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(kb_shared, lv_color_hex(0x161616), 0);
    lv_obj_add_flag(kb_shared, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(kb_shared, kb_hide, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb_shared, kb_hide, LV_EVENT_CANCEL, NULL);

    show_tab(0);
    apply_theme();
    debug_build();
}
