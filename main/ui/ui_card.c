/* Cartao da aeronave selecionada: logo, chamada, rota, numeros e foto. */
#include "ui_internal.h"

static bool starts_with_code(const char *flight, const char *code)
{
    if (!flight || !code || !code[0]) return false;
    for (int i = 0; code[i]; i++) {
        char a = flight[i];
        char b = code[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 32);
        if (b >= 'a' && b <= 'z') b = (char)(b - 32);
        if (a != b) return false;
    }
    return true;
}

static void callsign_text(const plane_t *p, bool has_logo, char *dst, int dst_n)
{
    if (flight_is_placeholder(p->flight)) {
        if (p->icao[0] && p->airline[0]) snprintf(dst, dst_n, "%s", p->airline);
        else snprintf(dst, dst_n, "%s", p->reg[0] ? p->reg : p->hex);
        return;
    }
    if (has_logo && starts_with_code(p->flight, p->icao)) {
        const char *rest = p->flight + strlen(p->icao);
        while (*rest == ' ') rest++;
        if (*rest) {
            snprintf(dst, dst_n, "%s", rest);
            return;
        }
    }
    snprintf(dst, dst_n, "%s", p->flight);
}

static void place_callsign(const char *text, const lv_image_dsc_t *logo)
{
    int x = 12;
    int row_y = 8;
    int row_h = 40;
    if (logo) {
        lv_image_set_src(img_logo, logo);
        lv_obj_set_pos(img_logo, 12, row_y);
        lv_obj_remove_flag(img_logo, LV_OBJ_FLAG_HIDDEN);
        x = 12 + (int)logo->header.w + 8;
    } else {
        lv_obj_add_flag(img_logo, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(lbl_brand, LV_OBJ_FLAG_HIDDEN);

    int avail = CARD_W - x - 12;
    if (avail < 40) avail = 40;
    const lv_font_t *fonts[3] = {
        &lv_font_montserrat_36, &lv_font_montserrat_28, &lv_font_montserrat_20
    };
    const lv_font_t *font = fonts[2];
    for (int i = 0; i < 3; i++) {
        int32_t tw = lv_text_get_width(text, (uint32_t)strlen(text), fonts[i], 0);
        if (tw <= avail) {
            font = fonts[i];
            break;
        }
    }
    lv_obj_set_style_text_font(lbl_flight, font, 0);
    lv_label_set_long_mode(lbl_flight, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_flight, avail);
    lv_label_set_text(lbl_flight, text);
    int y = row_y + (row_h - (int)font->line_height) / 2;
    lv_obj_set_pos(lbl_flight, x, y);
}

void refresh_card(void)
{
    const plane_t *p = (s_view_sel >= 0 && s_view_sel < s_view_n) ? &s_view[s_view_sel] : NULL;
    if (!p) {
        lv_obj_add_flag(img_logo, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_brand, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(img_photo, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lbl_credit, "");
        place_callsign("Aguardando", NULL);
        lv_label_set_text_fmt(lbl_meta, "Nenhum aviao no raio de %.0f NM", cfg_range_nm());
        lv_label_set_text(lbl_from, "");
        lv_label_set_text(lbl_to, "");
        lv_obj_add_flag(route_col, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 4; i++) lv_obj_add_flag(stat_box[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(lbl_count, "%02d ACFT", s_view_n > 99 ? 99 : s_view_n);
        return;
    }

    const lv_image_dsc_t *logo = airline_logo(p->icao);
    char title[16];
    callsign_text(p, logo != NULL, title, sizeof(title));
    place_callsign(title, logo);

    char meta[96];
    if (flight_is_placeholder(p->flight)) {
        if (p->icao[0] && p->reg[0] && p->desc[0]) {
            snprintf(meta, sizeof(meta), "%s   %s", p->reg, p->desc);
        } else if (p->icao[0] && p->reg[0]) {
            snprintf(meta, sizeof(meta), "%s", p->reg);
        } else {
            snprintf(meta, sizeof(meta), "%s", p->desc[0] ? p->desc : "");
        }
    } else if (p->reg[0] && p->desc[0]) {
        snprintf(meta, sizeof(meta), "%s   %s", p->reg, p->desc);
    } else if (p->desc[0]) {
        snprintf(meta, sizeof(meta), "%s", p->desc);
    } else {
        snprintf(meta, sizeof(meta), "%s", p->reg[0] ? p->reg : p->hex);
    }
    lv_label_set_text(lbl_meta, meta);

    lv_obj_remove_flag(route_col, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lbl_from, p->origin[0] ? p->origin : "--");
    lv_label_set_text(lbl_to, p->dest[0] ? p->dest : "--");
    if (p->origin[0] && p->dest[0]) stat_route_visible(p->hex);
    lv_obj_set_style_text_color(lbl_from, lv_color_hex(p->origin[0] ? COL_TEXT : COL_MUTED), 0);
    lv_obj_set_style_text_color(lbl_to, lv_color_hex(p->dest[0] ? COL_TEXT : COL_MUTED), 0);

    char alt[16] = "--";
    char gs[16] = "--";
    char tr[16] = "--";
    char di[16] = "--";
    const char *alt_u = "";
    const char *gs_u = "";
    const char *di_u = "";
    if (p->on_ground) {
        snprintf(alt, sizeof(alt), "GND");
    } else if (p->alt_ft >= 0) {
        snprintf(alt, sizeof(alt), "%d", p->alt_ft);
        alt_u = "ft";
    }
    if (p->gs_kt >= 0) {
        snprintf(gs, sizeof(gs), "%d", p->gs_kt);
        gs_u = "kt";
    }
    if (p->track_deg >= 0) snprintf(tr, sizeof(tr), "%03d", (int)(p->track_deg + 0.5f) % 360);
    if (p->has_pos) {
        snprintf(di, sizeof(di), "%.1f", p->dist_nm);
        di_u = "nm";
    }
    const char *vals[4] = {alt, gs, tr, di};
    const char *units[4] = {alt_u, gs_u, "", di_u};
    for (int i = 0; i < 4; i++) {
        lv_label_set_text(stat_val[i], vals[i]);
        if (stat_unit[i]) lv_label_set_text(stat_unit[i], units[i]);
        lv_obj_remove_flag(stat_box[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text_fmt(lbl_count, "%02d ACFT", s_view_n > 99 ? 99 : s_view_n);
}

void apply_photo(void)
{
    const uint16_t *rgb = NULL;
    int w = 0, h = 0;
    char credit[48];
    uint32_t seq = model_photo(&rgb, &w, &h, credit, sizeof(credit));
    if (seq == s_photo_seen) return;
    s_photo_seen = seq;
    if (rgb && w > 0 && h > 0) {
        memset(&s_photo_dsc, 0, sizeof(s_photo_dsc));
        s_photo_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        s_photo_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        s_photo_dsc.header.w = (uint32_t)w;
        s_photo_dsc.header.h = (uint32_t)h;
        s_photo_dsc.header.stride = (uint32_t)w * 2;
        s_photo_dsc.data_size = (uint32_t)w * (uint32_t)h * 2;
        s_photo_dsc.data = (const uint8_t *)rgb;
        lv_image_set_src(img_photo, &s_photo_dsc);
        int x = (CARD_W - w) / 2;
        int y = PHOTO_AREA_Y + (PHOTO_AREA_H - h) / 2;
        if (x < 0) x = 0;
        if (y < PHOTO_AREA_Y) y = PHOTO_AREA_Y;
        lv_obj_set_pos(img_photo, x, y);
        lv_obj_remove_flag(img_photo, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lbl_credit, credit[0] ? credit : "");
        lv_obj_set_pos(lbl_credit, 8, PHOTO_AREA_Y + PHOTO_AREA_H + 2);
        if (s_view_sel >= 0 && s_view_sel < s_view_n) stat_photo_visible(s_view[s_view_sel].hex);
    } else {
        lv_obj_add_flag(img_photo, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lbl_credit, "");
    }
}

void make_stat(lv_obj_t *parent, int index, int x, int y, int w, int h, const char *cap)
{
    lv_obj_t *box = lv_obj_create(parent);
    stat_box[index] = box;
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x262626), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_border_side(box, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(box, 3, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(theme_accent()), 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_shadow_width(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *c = lv_label_create(box);
    lv_label_set_text(c, cap);
    lv_obj_set_style_text_font(c, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(c, lv_color_hex(theme_accent()), 0);

    lv_obj_t *v = lv_label_create(box);
    stat_val[index] = v;
    lv_label_set_text(v, "--");
    lv_obj_set_style_text_color(v, lv_color_hex(COL_TEXT), 0);
    stat_unit[index] = NULL;

    lv_obj_set_pos(c, 8, 3);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_20, 0);
    lv_obj_set_width(v, w - 14);
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(v, 8, 22);

    lv_obj_t *u = lv_label_create(box);
    stat_unit[index] = u;
    lv_label_set_text(u, "");
    lv_obj_set_style_text_font(u, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(u, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(u, LV_ALIGN_TOP_RIGHT, -8, 4);
}

/* Coluna de rota: circulo aberto na origem, ponto cheio no destino. */
static void draw_route_col(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_coords(obj, &c);
    int x = (c.x1 + c.x2) / 2;
    int y_from = c.y1 + 8;
    int y_to = c.y2 - 8;

    lv_draw_line_dsc_t ln;
    lv_draw_line_dsc_init(&ln);
    ln.color = lv_color_hex(theme_accent());
    ln.width = 2;
    ln.p1.x = x;
    ln.p1.y = y_from + 6;
    ln.p2.x = x;
    ln.p2.y = y_to - 5;
    lv_draw_line(layer, &ln);

    lv_draw_rect_dsc_t ring;
    lv_draw_rect_dsc_init(&ring);
    ring.bg_opa = LV_OPA_TRANSP;
    ring.border_color = lv_color_hex(theme_accent());
    ring.border_width = 2;
    ring.border_opa = LV_OPA_COVER;
    ring.radius = LV_RADIUS_CIRCLE;
    lv_area_t a = {x - 5, y_from - 5, x + 5, y_from + 5};
    lv_draw_rect(layer, &ring, &a);

    lv_draw_rect_dsc_t dot;
    lv_draw_rect_dsc_init(&dot);
    dot.bg_color = lv_color_hex(theme_accent());
    dot.bg_opa = LV_OPA_COVER;
    dot.radius = LV_RADIUS_CIRCLE;
    lv_area_t b = {x - 4, y_to - 4, x + 4, y_to + 4};
    lv_draw_rect(layer, &dot, &b);
}

lv_obj_t *make_route_col(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, 12, 73);
    lv_obj_set_size(obj, 16, 42);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(obj, draw_route_col, LV_EVENT_DRAW_MAIN, NULL);
    return obj;
}
