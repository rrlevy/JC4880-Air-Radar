/* Toque, gesto e zoom. A paisagem usa PPA 270: lx = y nativo,
   ly = 479 - x nativo. Nao inverter de novo. */
#include "ui_internal.h"

static void map_touch(int nx, int ny, int *lx, int *ly)
{
    /* Paisagem com PPA 270: lx = y nativo, ly = 479 - x nativo. */
    int x = ny;
    int y = 479 - nx;
    if (x < 0) x = 0;
    if (x > SCR_W - 1) x = SCR_W - 1;
    if (y < 0) y = 0;
    if (y > SCR_H - 1) y = SCR_H - 1;
    *lx = x;
    *ly = y;
}

void touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (!s_screen_on) {
        data->state = LV_INDEV_STATE_RELEASED;
        s_press_y = -1;
        s_pinching = false;
        s_close_armed = false;
        s_swipe_y0 = -1;
        return;
    }
    int xs[2] = {0, 0};
    int ys[2] = {0, 0};
    int n = board_p4_touch_read_n(xs, ys, 2);
    if (n <= 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        s_press_y = -1;
        s_pinching = false;
        s_close_armed = false;
        s_swipe_y0 = -1;
        return;
    }
    int lx = 0, ly = 0;
    map_touch(xs[0], ys[0], &lx, &ly);
    if (n >= 2) {
        int lx2 = 0, ly2 = 0;
        map_touch(xs[1], ys[1], &lx2, &ly2);
        int dx = lx - lx2;
        int dy = ly - ly2;
        int dist = (int)sqrtf((float)dx * (float)dx + (float)dy * (float)dy);
        if (!s_pinching) {
            s_pinching = true;
            s_pinch_d0 = dist > 12 ? dist : 12;
            s_pinch_idx = cfg_range_index();
            s_press_y = -1;
        } else if (s_pinch_d0 > 0) {
            float ratio = (float)dist / (float)s_pinch_d0;
            int steps_in = 0;
            float r = ratio;
            while (r >= 1.22f && steps_in < 9) {
                r /= 1.22f;
                steps_in++;
            }
            int steps_out = 0;
            r = ratio;
            while (r <= 0.82f && steps_out < 9) {
                r /= 0.82f;
                steps_out++;
            }
            cfg_set_range_index(s_pinch_idx - steps_in + steps_out);
        }
        data->point.x = (lx + lx2) / 2;
        data->point.y = (ly + ly2) / 2;
        /* Solto para o LVGL, para a pinca nao virar clique nem puxar o menu. */
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    if (s_pinching) {
        s_pinching = false;
        data->state = LV_INDEV_STATE_RELEASED;
        s_press_y = -1;
        return;
    }
    if (s_press_y < 0) {
        s_press_from_top = ly < 96;
        s_swipe_x0 = lx;
        s_swipe_y0 = ly;
        s_close_armed = false;
        /* Oposto de abrir: arrastar para cima fecha. A lista de redes continua rolando. */
        if (lv_screen_active() == scr_wifi) {
            bool on_list = (s_tab == 0 && lx < 390 && ly > 70);
            if (!on_list || ly > SCR_H - 80) s_close_armed = true;
        }
    }
    if (s_close_armed && s_swipe_y0 >= 0) {
        int up = s_swipe_y0 - ly;
        int adx = lx - s_swipe_x0;
        if (adx < 0) adx = -adx;
        if (up > 64 && adx < 110 && adx < up) {
            s_close_req = true;
            s_close_armed = false;
            s_press_y = -1;
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }
    }
    s_press_y = ly;
    data->point.x = lx;
    data->point.y = ly;
    data->state = LV_INDEV_STATE_PRESSED;
}

void on_gesture(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    if (lv_indev_get_gesture_dir(indev) != LV_DIR_BOTTOM) return;
    if (lv_screen_active() != scr_radar) return;
    if (!s_press_from_top) return;
    lv_indev_wait_release(indev);
    open_wifi_screen();
}

void on_zoom(lv_event_t *e)
{
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    if (dir > 0) cfg_zoom_in();
    else cfg_zoom_out();
}
