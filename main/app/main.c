/* Arranque. NVS, configuracao, modelo, tela, toque, Wi-Fi e a tarefa de rede.
   A gravacao de fabrica nao pode apagar a NVS: as chaves do usuario moram la. */
#include "cfg.h"
#include "model.h"
#include "net.h"
#include "ui.h"
#include "wifi_app.h"

#include "board_p4.h"
#include "esp_bit_defs.h"
#include "esp_heap_caps.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_private/startup_internal.h"
#include "nvs_flash.h"

static const char *TAG = "app";

ESP_SYSTEM_INIT_FN(log_internal_ram, SECONDARY, BIT(0), 230)
{
    unsigned free_b = (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    unsigned big = (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_EARLY_LOGI("boot", "ram interna livre %u maior %u", free_b, big);
    return ESP_OK;
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    cfg_init();
    model_init();

    err = board_p4_display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display: %s", esp_err_to_name(err));
        return;
    }
    board_p4_set_brightness(230);

    err = board_p4_touch_init();
    if (err != ESP_OK) ESP_LOGW(TAG, "touch: %s", esp_err_to_name(err));

    wifi_app_start();
    ui_start();
    net_start();
}
