/* Resumo de tempos. So aparece com a tela aberta. */
#include "ui_internal.h"

#include "stat.h"

static lv_obj_t *scr_debug;
static lv_obj_t *lbl_debug;
static int64_t s_drawn_us;

static void fmt_ms(char *dst, int n, int ms)
{
    if (ms < 0) snprintf(dst, n, "--");
    else if (ms < 10000) snprintf(dst, n, "%d ms", ms);
    else snprintf(dst, n, "%d.%d s", ms / 1000, (ms % 1000) / 100);
}

static void line_http(char *dst, int n, const char *name, const stat_http_t *h)
{
    char avg[16], last[16];
    fmt_ms(avg, sizeof(avg), h->avg_ms);
    fmt_ms(last, sizeof(last), h->last_ms);
    snprintf(dst, n, "%-13s media %s  ult %s  n %d  falhas %d  vazio %d",
             name, avg, last, h->n, h->fails, h->empty);
}

static void step_bit(char *dst, int n, const char *name, const stat_step_t *s)
{
    if (!s || s->result < 0) snprintf(dst, n, "%s --", name);
    else if (s->result == STAT_RES_OK) snprintf(dst, n, "%s ok %dms", name, s->ms);
    else if (s->result == STAT_RES_EMPTY) snprintf(dst, n, "%s vazio %dms", name, s->ms);
    else if (s->result == STAT_RES_LATER) snprintf(dst, n, "%s espera %dms", name, s->ms);
    else snprintf(dst, n, "%s falha %dms", name, s->ms);
}

static void line_lag(char *dst, int n, const char *name, bool pending, bool skip, int ms, int wait_ms)
{
    if (skip) snprintf(dst, n, "%-14s n/a", name);
    else if (pending) {
        char w[16];
        fmt_ms(w, sizeof(w), wait_ms);
        snprintf(dst, n, "%-14s aguardando %s", name, w);
    } else {
        char v[16];
        fmt_ms(v, sizeof(v), ms);
        snprintf(dst, n, "%-14s %s", name, v);
    }
}

static void on_back(lv_event_t *e)
{
    (void)e;
    show_tab(3);
    lv_screen_load(scr_wifi);
}

void debug_build(void)
{
    scr_debug = lv_obj_create(NULL);
    style_screen(scr_debug);

    lv_obj_t *title = lv_label_create(scr_debug);
    lv_label_set_text(title, "Debug");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(title, 16, 12);

    lv_obj_t *back = make_button(scr_debug, "Voltar");
    lv_obj_set_pos(back, 640, 12);
    lv_obj_set_size(back, 140, 40);
    lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, NULL);

    lbl_debug = lv_label_create(scr_debug);
    lv_obj_set_pos(lbl_debug, 16, 68);
    lv_obj_set_width(lbl_debug, 768);
    lv_obj_set_style_text_font(lbl_debug, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_debug, lv_color_hex(COL_TEXT), 0);
    lv_label_set_text(lbl_debug, "");
}

bool debug_is_open(void)
{
    return scr_debug && lv_screen_active() == scr_debug;
}

void debug_refresh(void)
{
    if (!lbl_debug || !debug_is_open()) return;
    int64_t now = esp_timer_get_time();
    if (now - s_drawn_us < 400000LL) return;
    s_drawn_us = now;

    stat_view_t st;
    stat_copy(&st);
    int on_screen = 0, route_ok = 0, route_need = 0, photo_ok = 0, ready = 0;
    model_cache_counts(&on_screen, &route_ok, &route_need, &photo_ok, &ready);

    char a[120], b[120], c[120], d[120], e[96], f[96];
    line_http(a, sizeof(a), "Voos gerais", &st.http[STAT_ADSB]);
    line_http(b, sizeof(b), "Rota", &st.http[STAT_ROUTE]);
    line_http(c, sizeof(c), "Foto", &st.http[STAT_PHOTO]);
    line_http(d, sizeof(d), "Dono", &st.http[STAT_OWNER]);
    line_lag(e, sizeof(e), "Rota na tela", st.route_pending, st.opened && !st.want_route, st.route_ms, st.wait_ms);
    line_lag(f, sizeof(f), "Imagem", st.photo_pending, false, st.photo_ms, st.wait_ms);

    model_dbg_t plane;
    bool have = model_debug_selected(&plane);
    stat_trace_t tr;
    stat_trace_copy(have ? plane.hex : "", have ? plane.flight : "", &tr);

    char adsb[64], route[128], p1[48], p2[48], p3[48], j1[48], c1[48], j2[48], c2[48];
    step_bit(adsb, sizeof(adsb), "ADS-B", &(stat_step_t){tr.adsb_ms, tr.adsb_res, ""});
    step_bit(route, sizeof(route), "adsbdb", &tr.step[TRACE_ROUTE]);
    step_bit(p1, sizeof(p1), "ps/hex", &tr.step[TRACE_PS_HEX]);
    step_bit(p2, sizeof(p2), "ps/reg", &tr.step[TRACE_PS_REG]);
    step_bit(p3, sizeof(p3), "airport", &tr.step[TRACE_AIRPORT]);
    step_bit(j1, sizeof(j1), "jpg p", &tr.step[TRACE_JPG_S]);
    step_bit(c1, sizeof(c1), "conv p", &tr.step[TRACE_DEC_S]);
    step_bit(j2, sizeof(j2), "jpg g", &tr.step[TRACE_JPG_L]);
    step_bit(c2, sizeof(c2), "conv g", &tr.step[TRACE_DEC_L]);

    char id[96];
    char raw[160];
    char kin[96];
    char cache[64];
    if (!have) {
        snprintf(id, sizeof(id), "nenhuma selecionada");
        raw[0] = 0;
        kin[0] = 0;
        cache[0] = 0;
    } else {
        snprintf(id, sizeof(id), "%s  %s  %s  %s",
                 plane.hex[0] ? plane.hex : "--",
                 plane.flight[0] ? plane.flight : "--",
                 plane.reg[0] ? plane.reg : "--",
                 plane.type_code[0] ? plane.type_code : "--");
        snprintf(raw, sizeof(raw), "alt %d  gs %d  trk %03.0f  dist %.1f NM  %s",
                 plane.alt_ft, plane.gs_kt, plane.track_deg < 0 ? 0 : plane.track_deg, plane.dist_nm,
                 plane.desc[0] ? plane.desc : plane.airline);
        snprintf(kin, sizeof(kin), "%s  >  %s",
                 plane.origin[0] ? plane.origin : "--",
                 plane.dest[0] ? plane.dest : "--");
        if (!plane.photo_cached) snprintf(cache, sizeof(cache), "cache convertido: nao");
        else snprintf(cache, sizeof(cache), "cache convertido: sim %dx%d %s",
                      plane.photo_w, plane.photo_h, plane.photo_full ? "grande" : "miniatura");
    }
    if (tr.step[TRACE_ROUTE].note[0]) {
        snprintf(route + strlen(route), sizeof(route) - strlen(route), "  %s", tr.step[TRACE_ROUTE].note);
    }

    char open[48];
    if (!st.opened) snprintf(open, sizeof(open), "nenhuma");
    else snprintf(open, sizeof(open), "%s", st.hex);

    char body[2048];
    snprintf(body, sizeof(body),
             "%s\n%s\n%s\n%s\n"
             "Ultima abertura %s   %s   %s\n"
             "Pre-busca %d/%d   rotas %d/%d   fotos %d/%d\n"
             "\nSelecionada  %s\n"
             "%s\n"
             "%s\n"
             "Rota  %s\n"
             "%s\n"
             "Foto  %s   %s   %s\n"
             "%s   %s   %s   %s\n"
             "%s",
             a, b, c, d, open, e, f,
             ready, on_screen, route_ok, route_need, photo_ok, on_screen,
             id, raw, adsb, route, kin, p1, p2, p3, j1, c1, j2, c2, cache);
    lv_label_set_text(lbl_debug, body);
}

void debug_open(void)
{
    if (!scr_debug) return;
    s_drawn_us = 0;
    lv_screen_load(scr_debug);
    debug_refresh();
}
