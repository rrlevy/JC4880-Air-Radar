/* Copia da tela pela serial USB. O log fica mudo durante o envio
   porque a consola usa o mesmo fio. Comandos de texto entram por aqui
   e a tarefa de animacao em ui.c e quem aplica. */
#include "ui_internal.h"

static const char *TAG = "ui";

void ui_request_screenshot(void)
{
    if (s_shot_buf) s_shot_req = true;
}

static int shot_silent(const char *fmt, va_list ap)
{
    (void)fmt;
    (void)ap;
    return 0;
}

static int shot_write_all(const uint8_t *p, size_t left)
{
    int stalls = 0;
    while (left) {
        size_t chunk = left > (size_t)s_shot_chunk ? (size_t)s_shot_chunk : left;
        int w = usb_serial_jtag_write_bytes(p, chunk, pdMS_TO_TICKS(2000));
        if (w <= 0) {
            if (++stalls > 6) return -1;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        stalls = 0;
        p += (size_t)w;
        left -= (size_t)w;
    }
    return 0;
}

static void shot_send(void)
{
    /* A consola USB escreve no mesmo fio. Sem isto, um log no meio desloca o quadro. */
    vprintf_like_t prev = esp_log_set_vprintf(shot_silent);
    vTaskDelay(pdMS_TO_TICKS(120));
    char hdr[32];
    int hn = snprintf(hdr, sizeof(hdr), "@@SHOT %d %d\n", SCR_W, SCR_H);
    bool ok = shot_write_all((const uint8_t *)hdr, (size_t)hn) == 0;
    const int row_bytes = SCR_W * (int)sizeof(uint16_t);
    for (int y = 0; ok && y < SCR_H; y++) {
        char rh[16];
        int rn = snprintf(rh, sizeof(rh), "@@R %04d\n", y);
        const uint8_t *row = (const uint8_t *)s_shot_buf + (size_t)y * (size_t)row_bytes;
        uint8_t sum = 0;
        for (int i = 0; i < row_bytes; i++) sum ^= row[i];
        if (shot_write_all((const uint8_t *)rh, (size_t)rn) != 0 ||
            shot_write_all(row, (size_t)row_bytes) != 0 ||
            shot_write_all(&sum, 1) != 0) {
            ok = false;
        }
    }
    usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(2000));
    usb_serial_jtag_write_bytes("\n@@END\n", 7, pdMS_TO_TICKS(1000));
    usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000));
    esp_log_set_vprintf(prev);
    if (ok) ESP_LOGI(TAG, "screenshot enviado");
    else ESP_LOGW(TAG, "screenshot incompleto");
}

static void list_send(void)
{
    static model_pick_t rows[PLANE_MAX];
    int n = model_pick_copy(rows, PLANE_MAX);
    vprintf_like_t prev = esp_log_set_vprintf(shot_silent);
    vTaskDelay(pdMS_TO_TICKS(80));
    char line[48];
    int hn = snprintf(line, sizeof(line), "@@LIST %d\n", n);
    bool ok = hn > 0 && shot_write_all((const uint8_t *)line, (size_t)hn) == 0;
    for (int i = 0; ok && i < n; i++) {
        const char *icao = rows[i].icao[0] ? rows[i].icao : "-";
        hn = snprintf(line, sizeof(line), "@@A %s %s %d %d\n",
                      rows[i].hex, icao, rows[i].dist_x10, rows[i].photo ? 1 : 0);
        if (hn < 0 || shot_write_all((const uint8_t *)line, (size_t)hn) != 0) ok = false;
    }
    if (shot_write_all((const uint8_t *)"@@ENDLIST\n", 10) != 0) ok = false;
    usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000));
    esp_log_set_vprintf(prev);
    if (!ok) ESP_LOGW(TAG, "lista incompleta");
}

static bool hex_ok(const char *s)
{
    int n = 0;
    for (; s[n]; n++) {
        char c = s[n];
        bool dig = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!dig) return false;
    }
    return n >= 4 && n <= 6;
}

void shot_task(void *arg)
{
    (void)arg;
    usb_serial_jtag_driver_config_t cfg = {
        .tx_buffer_size = 16384,
        .rx_buffer_size = 512,
    };
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err == ESP_OK) {
        s_shot_chunk = 1024;
    } else if (err == ESP_ERR_INVALID_STATE) {
        s_shot_chunk = 64;
        ESP_LOGW(TAG, "usb serial ja instalado, envio em blocos de 64");
    } else {
        s_shot_chunk = 64;
        ESP_LOGW(TAG, "usb serial jtag: %s", esp_err_to_name(err));
    }
    char acc[16];
    int n = 0;
    uint8_t b;
    while (1) {
        if (s_shot_ready && s_shot_buf) {
            s_shot_ready = false;
            shot_send();
        }
        int r = usb_serial_jtag_read_bytes(&b, 1, pdMS_TO_TICKS(30));
        if (r == 1) {
            if (b == '\n' || b == '\r') {
                acc[n] = 0;
                if (strcmp(acc, "shot") == 0) ui_request_screenshot();
                else if (strcmp(acc, "radar") == 0) s_nav = 1;
                else if (strncmp(acc, "menu ", 5) == 0 && acc[5] >= '0' && acc[5] <= '4' && acc[6] == 0) {
                    s_nav_arg = acc[5] - '0';
                    s_nav = 2;
                } else if (strncmp(acc, "nm ", 3) == 0 && acc[3]) {
                    s_nav_arg = range_index_for_nm(atoi(acc + 3));
                    s_nav = 3;
                } else if (strncmp(acc, "tema ", 5) == 0 && acc[5] >= '0' && acc[5] <= '3' && acc[6] == 0) {
                    s_nav_arg = acc[5] - '0';
                    s_nav = 4;
                } else if (strcmp(acc, "tela") == 0) {
                    s_nav = 5;
                } else if (strncmp(acc, "mapa ", 5) == 0 && (acc[5] == '0' || acc[5] == '1') && acc[6] == 0) {
                    s_nav_arg = acc[5] - '0';
                    s_nav = 6;
                } else if (strncmp(acc, "rios ", 5) == 0 && (acc[5] == '0' || acc[5] == '1') && acc[6] == 0) {
                    s_nav_arg = acc[5] - '0';
                    s_nav = 7;
                } else if (strncmp(acc, "avioes ", 7) == 0 && (acc[7] == '0' || acc[7] == '1') && acc[8] == 0) {
                    s_nav_arg = acc[7] - '0';
                    s_nav = 8;
                } else if (strncmp(acc, "heli ", 5) == 0 && (acc[5] == '0' || acc[5] == '1') && acc[6] == 0) {
                    s_nav_arg = acc[5] - '0';
                    s_nav = 9;
                } else if (strcmp(acc, "lista") == 0) {
                    list_send();
                } else if (strncmp(acc, "sel ", 4) == 0 && hex_ok(acc + 4)) {
                    model_select_hex(acc + 4);
                } else if (strcmp(acc, "solta") == 0) {
                    model_release_pin();
                }
                n = 0;
            } else if (n < (int)sizeof(acc) - 1) {
                acc[n++] = (char)b;
            } else {
                n = 0;
            }
        }
    }
}
