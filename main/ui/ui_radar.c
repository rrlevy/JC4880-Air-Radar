/* Desenho do radar: anel, cruz, mapa, rios, aeroportos, avioes,
   pontos cardeais e os botoes de zoom. As letras N E S W ficam
   fora do anel (raio + 22). A cor do rio nao segue o tema. */
#include "ui_internal.h"

static const char *TAG = "ui";

static void draw_ring(lv_layer_t *layer, int cx, int cy, int r, lv_color_t color, int width)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_TRANSP;
    d.border_color = color;
    d.border_width = width;
    d.border_opa = LV_OPA_COVER;
    d.radius = LV_RADIUS_CIRCLE;
    lv_area_t a = {cx - r, cy - r, cx + r, cy + r};
    lv_draw_rect(layer, &d, &a);
}

static void draw_text(lv_layer_t *layer, int x, int y, const char *text, lv_color_t color,
                      const lv_font_t *font, int half_w)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.color = color;
    d.font = font;
    d.text = text;
    d.align = LV_TEXT_ALIGN_CENTER;
    lv_area_t a = {x - half_w, y - 10, x + half_w, y + 14};
    lv_draw_label(layer, &d, &a);
}

static uint32_t blip_color(const char *icao)
{
    return model_blip_color(icao);
}

/* Proa 000: norte para cima. 090: leste para cima. right/up no plano da tela. */
static void map_px(float east, float north, int heading_deg, float px_scale,
                   int cx, int cy, int *ox, int *oy)
{
    float th = (float)heading_deg * (float)M_PI / 180.0f;
    float c = cosf(th);
    float s = sinf(th);
    float right = east * c - north * s;
    float up = east * s + north * c;
    *ox = cx + (int)(right * px_scale);
    *oy = cy - (int)(up * px_scale);
}

/* Teto de rotulos no anel. O circulo tem o mesmo tamanho em qualquer alcance. */
#define APT_DRAW_MAX 12
/* Dois simbolos mais perto que isto ficam com um so. 28 px e a altura do rotulo. */
#define APT_SEP_PX 28

typedef struct {
    const apt_t *apt;
    float dist;
    int x;
    int y;
} apt_cand_t;

/* 250 NM: grande e medio. Abaixo: tambem o local. */
static int apt_max_rank(float range_nm)
{
    if (range_nm >= 250.f) return APT_RANK_MEDIUM;
    return APT_RANK_SMALL;
}

/* true se a deve ser desenhado depois de b, isto e, perde o desempate. */
static bool apt_after(const apt_cand_t *a, const apt_cand_t *b)
{
    if (a->apt->rank != b->apt->rank) return a->apt->rank > b->apt->rank;
    if (a->apt->rwy_m != b->apt->rwy_m) return a->apt->rwy_m < b->apt->rwy_m;
    return a->dist > b->dist;
}

static void draw_apt_mark(lv_layer_t *layer, int x, int y, const char *iata)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = lv_color_hex(COL_APT);
    d.width = 2;
    const int s = 3;
    d.p1.x = x; d.p1.y = y - s; d.p2.x = x + s; d.p2.y = y;
    lv_draw_line(layer, &d);
    d.p1.x = x + s; d.p1.y = y; d.p2.x = x; d.p2.y = y + s;
    lv_draw_line(layer, &d);
    d.p1.x = x; d.p1.y = y + s; d.p2.x = x - s; d.p2.y = y;
    lv_draw_line(layer, &d);
    d.p1.x = x - s; d.p1.y = y; d.p2.x = x; d.p2.y = y - s;
    lv_draw_line(layer, &d);
    draw_text(layer, x + 26, y - 1, iata, lv_color_hex(COL_APT), &lv_font_montserrat_14, 22);
}

static bool apt_project(const apt_t *a, int max_rank, double lat0, double lon0,
                        int heading, float scale, float range_nm,
                        int cx, int cy, int limit2, apt_cand_t *out)
{
    if (a->rank < 1 || a->rank > max_rank) return false;
    float brg = 0, east = 0, north = 0, dist = 0;
    geo_project_from(lat0, lon0, a->lat, a->lon, &dist, &brg, &east, &north);
    if (dist > range_nm) return false;
    int x, y;
    map_px(east, north, heading, scale, cx, cy, &x, &y);
    int dx = x - cx;
    int dy = y - cy;
    if (dx * dx + dy * dy > limit2) return false;
    out->apt = a;
    out->dist = dist;
    out->x = x;
    out->y = y;
    return true;
}

static void draw_airports(lv_layer_t *layer, int cx, int cy, int radius, float scale,
                          float range_nm, int heading)
{
    int n = 0;
    const apt_t *list = airports_all(&n);
    double lat0 = cfg_lat();
    double lon0 = cfg_lon();
    int limit2 = (radius + 8) * (radius + 8);
    int max_rank = apt_max_rank(range_nm);
    /* So os que cabem na tela. A tabela inteira fica na flash. */
    apt_cand_t keep[APT_DRAW_MAX];
    int drawn = 0;
    for (int slot = 0; slot < APT_DRAW_MAX; slot++) {
        apt_cand_t best;
        bool have = false;
        for (int i = 0; i < n; i++) {
            apt_cand_t c;
            if (!apt_project(&list[i], max_rank, lat0, lon0, heading, scale, range_nm,
                            cx, cy, limit2, &c)) {
                continue;
            }
            bool used = false;
            bool clash = false;
            for (int k = 0; k < drawn; k++) {
                if (keep[k].apt == c.apt) used = true;
                int dx = c.x - keep[k].x;
                int dy = c.y - keep[k].y;
                if (dx * dx + dy * dy < APT_SEP_PX * APT_SEP_PX) clash = true;
            }
            if (used || clash) continue;
            /* O primeiro lugar e o mais perto do centro. Os outros seguem a classe. */
            if (!have || (slot == 0 ? c.dist < best.dist : apt_after(&best, &c))) {
                best = c;
                have = true;
            }
        }
        if (!have) break;
        keep[drawn++] = best;
    }
    for (int k = 0; k < drawn; k++) {
        draw_apt_mark(layer, keep[k].x, keep[k].y, keep[k].apt->iata);
    }
}

static bool seg_near_nm(float e1, float n1, float e2, float n2, float range)
{
    float lim = range * range;
    if (e1 * e1 + n1 * n1 <= lim || e2 * e2 + n2 * n2 <= lim) return true;
    float vx = e2 - e1;
    float vy = n2 - n1;
    float len2 = vx * vx + vy * vy;
    if (len2 < 0.01f) return false;
    float t = -(e1 * vx + n1 * vy) / len2;
    if (t < 0.f) t = 0.f;
    else if (t > 1.f) t = 1.f;
    float px = e1 + t * vx;
    float py = n1 + t * vy;
    return px * px + py * py <= lim;
}

static bool clip_to_circle(float *x1, float *y1, float *x2, float *y2, float cx, float cy, float r)
{
    float ax = *x1 - cx;
    float ay = *y1 - cy;
    float dx = *x2 - *x1;
    float dy = *y2 - *y1;
    float a = dx * dx + dy * dy;
    float b = 2.f * (ax * dx + ay * dy);
    float c = ax * ax + ay * ay - r * r;
    if (a < 0.01f) return c <= 0.f;
    float disc = b * b - 4.f * a * c;
    if (disc < 0.f) return false;
    float s = sqrtf(disc);
    float inv = 0.5f / a;
    float t0 = (-b - s) * inv;
    float t1 = (-b + s) * inv;
    float lo = t0 > 0.f ? t0 : 0.f;
    float hi = t1 < 1.f ? t1 : 1.f;
    if (lo > hi) return false;
    float nx1 = *x1 + dx * lo;
    float ny1 = *y1 + dy * lo;
    *x2 = *x1 + dx * hi;
    *y2 = *y1 + dy * hi;
    *x1 = nx1;
    *y1 = ny1;
    return true;
}

static void draw_brazil(lv_layer_t *layer, int cx, int cy, int radius, float scale,
                        float range_nm, int heading)
{
    int nrings = 0;
    const uint16_t *rings = brazil_rings(&nrings);
    const int16_t *xy = brazil_coords();
    if (!rings || !xy || nrings <= 0) return;
    double lat0 = cfg_lat();
    double lon0 = cfg_lon();
    float cos0 = cosf((float)(lat0 * (M_PI / 180.0)));
    int cursor = 0;
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = lv_color_hex(0x555555);
    d.width = 1;
    for (int r = 0; r < nrings; r++) {
        int n = rings[r];
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            float lat1 = xy[(cursor + i) * 2] / 100.f;
            float lon1 = xy[(cursor + i) * 2 + 1] / 100.f;
            float lat2 = xy[(cursor + j) * 2] / 100.f;
            float lon2 = xy[(cursor + j) * 2 + 1] / 100.f;
            float e1 = (lon1 - (float)lon0) * cos0 * 60.f;
            float n1 = (lat1 - (float)lat0) * 60.f;
            float e2 = (lon2 - (float)lon0) * cos0 * 60.f;
            float n2 = (lat2 - (float)lat0) * 60.f;
            if (!seg_near_nm(e1, n1, e2, n2, range_nm)) continue;
            int ix1, iy1, ix2, iy2;
            map_px(e1, n1, heading, scale, cx, cy, &ix1, &iy1);
            map_px(e2, n2, heading, scale, cx, cy, &ix2, &iy2);
            float x1 = (float)ix1, y1 = (float)iy1, x2 = (float)ix2, y2 = (float)iy2;
            if (!clip_to_circle(&x1, &y1, &x2, &y2, (float)cx, (float)cy, (float)radius)) continue;
            d.p1.x = (int)x1;
            d.p1.y = (int)y1;
            d.p2.x = (int)x2;
            d.p2.y = (int)y2;
            lv_draw_line(layer, &d);
        }
        cursor += n;
    }
}

static void draw_rivers(lv_layer_t *layer, int cx, int cy, int radius, float scale,
                        float range_nm, int heading)
{
    int nruns = 0;
    const uint16_t *runs = river_runs(&nruns);
    const int16_t *xy = river_coords();
    if (!runs || !xy || nruns <= 0) return;
    double lat0 = cfg_lat();
    double lon0 = cfg_lon();
    float cos0 = cosf((float)(lat0 * (M_PI / 180.0)));
    int cursor = 0;
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    /* Azul acinzentado, mais escuro que o contorno 0x555555, para nao competir com ele. */
    d.color = lv_color_hex(0x3A4550);
    d.width = 1;
    for (int r = 0; r < nruns; r++) {
        int n = runs[r];
        for (int i = 0; i < n - 1; i++) {
            float lat1 = xy[(cursor + i) * 2] / 400.f;
            float lon1 = xy[(cursor + i) * 2 + 1] / 400.f;
            float lat2 = xy[(cursor + i + 1) * 2] / 400.f;
            float lon2 = xy[(cursor + i + 1) * 2 + 1] / 400.f;
            float e1 = (lon1 - (float)lon0) * cos0 * 60.f;
            float n1 = (lat1 - (float)lat0) * 60.f;
            float e2 = (lon2 - (float)lon0) * cos0 * 60.f;
            float n2 = (lat2 - (float)lat0) * 60.f;
            if (!seg_near_nm(e1, n1, e2, n2, range_nm)) continue;
            int ix1, iy1, ix2, iy2;
            map_px(e1, n1, heading, scale, cx, cy, &ix1, &iy1);
            map_px(e2, n2, heading, scale, cx, cy, &ix2, &iy2);
            float x1 = (float)ix1, y1 = (float)iy1, x2 = (float)ix2, y2 = (float)iy2;
            if (!clip_to_circle(&x1, &y1, &x2, &y2, (float)cx, (float)cy, (float)radius)) continue;
            d.p1.x = (int)x1;
            d.p1.y = (int)y1;
            d.p2.x = (int)x2;
            d.p2.y = (int)y2;
            lv_draw_line(layer, &d);
        }
        cursor += n;
    }
}

/* Vista de cima. Tres pas no disco. O nariz, curto, aponta o voo.
   A cauda e mais longa e fecha no rotor de cauda: isso marca a traseira. */
static void draw_heli(lv_layer_t *layer, int x, int y, float rad, lv_color_t color, bool sel)
{
    const int disc = sel ? 8 : 6;
    const int nose = sel ? 10 : 8;
    const int tail = sel ? 13 : 12;
    const int span = sel ? 4 : 3;

    draw_ring(layer, x, y, disc, color, 2);

    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = color;
    d.width = 2;
    for (int b = 0; b < 3; b++) {
        float a = rad + 0.55f + (float)b * (2.0f * (float)M_PI / 3.0f);
        float reach = (float)(disc - 1);
        d.p1.x = x;
        d.p1.y = y;
        d.p2.x = x + (int)(sinf(a) * reach);
        d.p2.y = y - (int)(cosf(a) * reach);
        lv_draw_line(layer, &d);
    }

    d.width = sel ? 3 : 2;
    d.p1.x = x + (int)(sinf(rad) * (float)nose);
    d.p1.y = y - (int)(cosf(rad) * (float)nose);
    d.p2.x = x - (int)(sinf(rad) * (float)tail);
    d.p2.y = y + (int)(cosf(rad) * (float)tail);
    lv_draw_line(layer, &d);

    float side = rad + (float)M_PI * 0.5f;
    int tx = d.p2.x;
    int ty = d.p2.y;
    d.width = 2;
    d.p1.x = tx + (int)(sinf(side) * (float)span);
    d.p1.y = ty - (int)(cosf(side) * (float)span);
    d.p2.x = tx - (int)(sinf(side) * (float)span);
    d.p2.y = ty + (int)(cosf(side) * (float)span);
    lv_draw_line(layer, &d);
}

static void radar_geom(int x1, int y1, int *cx, int *cy, int *radius)
{
    *cx = x1 + RADAR_CX;
    *cy = y1 + RADAR_CY;
    *radius = RADAR_R;
}

void radar_draw(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    int cx, cy, radius;
    radar_geom(coords.x1, coords.y1, &cx, &cy, &radius);

    draw_ring(layer, cx, cy, radius, lv_color_hex(theme_ring()), 2);

    int heading = cfg_heading_deg();
    float range_nm = cfg_range_nm();
    if (range_nm < 1.f) range_nm = 1.f;
    float scale = (float)radius / range_nm;
    if (cfg_show_map())
        draw_brazil(layer, cx, cy, radius, scale, range_nm, heading);
    if (cfg_show_rivers())
        draw_rivers(layer, cx, cy, radius, scale, range_nm, heading);

    /* Anel do proximo zoom mais proximo (o passo que o + aplica). */
    int step = cfg_range_index();
    if (step > 0) {
        float inner_nm = cfg_range_step(step - 1);
        int ir = (int)((float)radius * (inner_nm / range_nm) + 0.5f);
        if (ir > 8 && ir < radius - 2)
            draw_ring(layer, cx, cy, ir, lv_color_hex(theme_ring()), 1);
    }

    /* Norte-sul e leste-oeste acompanham o norte verdadeiro. */
    lv_draw_line_dsc_t cross;
    lv_draw_line_dsc_init(&cross);
    cross.color = lv_color_hex(theme_cross());
    cross.width = 1;
    int ax, ay, bx, by;
    map_px(0.f, 1.f, heading, (float)radius, cx, cy, &ax, &ay);
    map_px(0.f, -1.f, heading, (float)radius, cx, cy, &bx, &by);
    cross.p1.x = ax; cross.p1.y = ay; cross.p2.x = bx; cross.p2.y = by;
    lv_draw_line(layer, &cross);
    map_px(1.f, 0.f, heading, (float)radius, cx, cy, &ax, &ay);
    map_px(-1.f, 0.f, heading, (float)radius, cx, cy, &bx, &by);
    cross.p1.x = ax; cross.p1.y = ay; cross.p2.x = bx; cross.p2.y = by;
    lv_draw_line(layer, &cross);

    int heading_marker = heading;
    lv_color_t accent = lv_color_hex(theme_accent());
    if (heading_marker != 0) {
        char hdg_lbl[8];
        snprintf(hdg_lbl, sizeof(hdg_lbl), "%03d", heading_marker);
        draw_text(layer, cx, cy - radius - 2, hdg_lbl, accent, &lv_font_montserrat_14, 28);
    }
    char range[16];
    snprintf(range, sizeof(range), "%.0f NM", range_nm);
    /* Alcance dentro do anel. O S ficou do lado de fora. */
    draw_text(layer, cx, cy + radius - 26, range, accent, &lv_font_montserrat_14, 48);

    /* N, E, S, W fora do anel, no norte verdadeiro, em qualquer proa. */
    static const struct {
        float east;
        float north;
        const char *lbl;
    } card[4] = {
        {0.f, 1.f, "N"},
        {1.f, 0.f, "E"},
        {0.f, -1.f, "S"},
        {-1.f, 0.f, "W"},
    };
    const float dist = (float)(radius + 22);
    for (int i = 0; i < 4; i++) {
        int x, y;
        map_px(card[i].east, card[i].north, heading_marker, dist, cx, cy, &x, &y);
        draw_text(layer, x, y, card[i].lbl, accent, &lv_font_montserrat_14, 16);
    }

    /* Rastro continuo: fatias juntas que perdem o alfa atras da linha. */
    lv_draw_line_dsc_t sw;
    lv_draw_line_dsc_init(&sw);
    sw.color = lv_color_hex(theme_accent());
    const int trail_n = 22;
    const float trail_deg = 26.f;
    for (int i = trail_n; i >= 1; i--) {
        float t = (float)i / (float)trail_n;
        float deg = s_sweep - t * trail_deg;
        float rad = deg * (float)M_PI / 180.0f;
        float fade = (1.f - t) * (1.f - t);
        sw.width = 5;
        sw.opa = (lv_opa_t)(210.f * fade);
        if (sw.opa < 6) continue;
        sw.p1.x = cx;
        sw.p1.y = cy;
        sw.p2.x = cx + (int)(sinf(rad) * radius);
        sw.p2.y = cy - (int)(cosf(rad) * radius);
        lv_draw_line(layer, &sw);
    }
    {
        float rad = s_sweep * (float)M_PI / 180.0f;
        sw.width = 3;
        sw.opa = LV_OPA_COVER;
        sw.p1.x = cx;
        sw.p1.y = cy;
        sw.p2.x = cx + (int)(sinf(rad) * radius);
        sw.p2.y = cy - (int)(cosf(rad) * radius);
        lv_draw_line(layer, &sw);
    }

    draw_airports(layer, cx, cy, radius, scale, range_nm, heading);
    /* O mais proximo e desenhado por ultimo, por cima dos demais. */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < s_view_n; i++) {
            const plane_t *p = &s_view[i];
            if (!p->has_pos) continue;
            bool sel = i == s_view_sel;
            if (sel != (pass == 1)) continue;
            int x, y;
            map_px(p->east_nm, p->north_nm, heading, scale, cx, cy, &x, &y);
            float arm = sel ? 14.0f : 9.0f;
            float base = sel ? 10.0f : 7.0f;

            if (sel) {
                lv_draw_line_dsc_t arrow;
                lv_draw_line_dsc_init(&arrow);
                arrow.color = lv_color_hex(theme_accent());
                arrow.width = 2;
                arrow.p1.x = cx;
                arrow.p1.y = cy;
                arrow.p2.x = x;
                arrow.p2.y = y;
                lv_draw_line(layer, &arrow);
                draw_ring(layer, x, y, 16, lv_color_hex(theme_accent()), 3);
                const char *tag = !flight_is_placeholder(p->flight) ? p->flight
                                  : (p->reg[0] ? p->reg : p->hex);
                draw_text(layer, x, y - 22, tag, lv_color_hex(theme_accent()), &lv_font_montserrat_14, 48);
            }

            float trk = p->track_deg >= 0 ? p->track_deg : p->bearing_deg;
            float rad = (trk - (float)heading) * (float)M_PI / 180.0f;
            lv_color_t ink = lv_color_hex(blip_color(p->icao));
            if (p->heli) {
                draw_heli(layer, x, y, rad, ink, sel);
            } else {
                int nx = x + (int)(sinf(rad) * arm);
                int ny = y - (int)(cosf(rad) * arm);
                int lx = x + (int)(sinf(rad + 2.5f) * base);
                int ly = y - (int)(cosf(rad + 2.5f) * base);
                int rx = x + (int)(sinf(rad - 2.5f) * base);
                int ry = y - (int)(cosf(rad - 2.5f) * base);
                lv_draw_line_dsc_t tri;
                lv_draw_line_dsc_init(&tri);
                tri.color = ink;
                tri.width = sel ? 3 : 2;
                tri.p1.x = nx; tri.p1.y = ny; tri.p2.x = lx; tri.p2.y = ly; lv_draw_line(layer, &tri);
                tri.p1.x = lx; tri.p1.y = ly; tri.p2.x = rx; tri.p2.y = ry; lv_draw_line(layer, &tri);
                tri.p1.x = rx; tri.p1.y = ry; tri.p2.x = nx; tri.p2.y = ny; lv_draw_line(layer, &tri);
            }
        }
    }

    lv_draw_rect_dsc_t home;
    lv_draw_rect_dsc_init(&home);
    home.bg_color = lv_color_hex(theme_accent());
    home.bg_opa = LV_OPA_COVER;
    home.radius = LV_RADIUS_CIRCLE;
    lv_area_t ha = {cx - 4, cy - 4, cx + 4, cy + 4};
    lv_draw_rect(layer, &home, &ha);
}

void radar_click(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev || !radar) return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    lv_area_t coords;
    lv_obj_get_coords(radar, &coords);
    int cx, cy, radius;
    radar_geom(coords.x1, coords.y1, &cx, &cy, &radius);
    float range_nm = cfg_range_nm();
    if (range_nm < 1.f) range_nm = 1.f;
    float scale = (float)radius / range_nm;
    int heading = cfg_heading_deg();
    int best = -1;
    int best_d2 = 44 * 44;
    for (int i = 0; i < s_view_n; i++) {
        const plane_t *p = &s_view[i];
        if (!p->has_pos) continue;
        int x, y;
        map_px(p->east_nm, p->north_nm, heading, scale, cx, cy, &x, &y);
        int dx = pt.x - x;
        int dy = pt.y - y;
        int d2 = dx * dx + dy * dy;
        if (d2 <= best_d2) {
            best_d2 = d2;
            best = i;
        }
    }
    if (best < 0) {
        ESP_LOGI(TAG, "toque longe, segue a mais proxima");
        model_release_pin();
        return;
    }
    ESP_LOGI(TAG, "toque %s", s_view[best].hex);
    model_select_hex(s_view[best].hex);
    bool want_route = s_view[best].flight[0] && !flight_is_placeholder(s_view[best].flight);
    stat_plane_opened(s_view[best].hex, want_route);
}

static const char *const ZOOM_MINUS_PX[15] = {
    "...............",
    "...............",
    "...............",
    "...............",
    "...............",
    "...............",
    "...#########...",
    "...#########...",
    "...#########...",
    "...............",
    "...............",
    "...............",
    "...............",
    "...............",
    "...............",
};

static const char *const ZOOM_PLUS_PX[15] = {
    "...............",
    "......###......",
    "......###......",
    "......###......",
    "......###......",
    "......###......",
    "...#########...",
    "...#########...",
    "...#########...",
    "......###......",
    "......###......",
    "......###......",
    "......###......",
    "......###......",
    "...............",
};

static void blit_px(lv_layer_t *layer, int x0, int y0, const char *const *rows, lv_color_t color)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = color;
    d.bg_opa = LV_OPA_COVER;
    d.radius = 0;
    d.border_width = 0;
    for (int y = 0; y < 15; y++) {
        int x = 0;
        while (x < 15) {
            while (x < 15 && rows[y][x] != '#') x++;
            int x1 = x;
            while (x < 15 && rows[y][x] == '#') x++;
            if (x > x1) {
                lv_area_t a = {x0 + x1, y0 + y, x0 + x - 1, y0 + y};
                lv_draw_rect(layer, &d, &a);
            }
        }
    }
}

static void draw_zoom_btn(lv_event_t *e)
{
    int plus = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_coords(obj, &c);
    bool pressed = lv_obj_has_state(obj, LV_STATE_PRESSED);

    lv_draw_rect_dsc_t box;
    lv_draw_rect_dsc_init(&box);
    box.bg_color = lv_color_hex(pressed ? 0x333333 : 0x141414);
    box.bg_opa = LV_OPA_COVER;
    box.border_color = lv_color_hex(0x7A7A7A);
    box.border_width = 2;
    box.border_opa = LV_OPA_COVER;
    box.radius = 6;
    lv_area_t frame = {c.x1 + 1, c.y1 + 1, c.x2 - 1, c.y2 - 1};
    lv_draw_rect(layer, &box, &frame);

    int w = lv_obj_get_width(obj);
    int h = lv_obj_get_height(obj);
    int ox = c.x1 + (w - 1) / 2 - 7;
    int oy = c.y1 + (h - 1) / 2 - 7;
    blit_px(layer, ox, oy, plus ? ZOOM_PLUS_PX : ZOOM_MINUS_PX, lv_color_hex(COL_TEXT));
}

lv_obj_t *make_zoom_btn(lv_obj_t *parent, int x, int y, int dir)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, ZOOM_W, ZOOM_H);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, draw_zoom_btn, LV_EVENT_DRAW_MAIN, (void *)(intptr_t)(dir > 0));
    lv_obj_add_event_cb(btn, on_zoom, LV_EVENT_CLICKED, (void *)(intptr_t)dir);
    return btn;
}
