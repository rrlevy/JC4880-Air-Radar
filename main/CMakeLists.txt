idf_component_register(
    SRCS "app/main.c"
         "cfg/cfg.c"
         "model/model.c"
         "net/net.c"
         "wifi/wifi_app.c"
         "geo/airports.c" "geo/brazil.c" "geo/rivers.c"
         "tz/tz.c" "tz/tz_rule.c"
         "gfx/logos.c" "gfx/jpgd/jpgd.cpp" "gfx/jpgd_rgb565.cpp"
         "ui/ui.c" "ui/ui_radar.c" "ui/ui_card.c"
         "ui/ui_input.c" "ui/ui_menu.c" "ui/ui_shot.c" "ui/ui_debug.c"
         "stat/stat.c"
    INCLUDE_DIRS "app" "cfg" "model" "net" "wifi" "geo" "tz" "gfx" "gfx/jpgd" "ui" "stat"
    EMBED_FILES "gfx/logos/gol.bin" "gfx/logos/gol_hdr.bin" "gfx/logos/latam.bin"
                "gfx/logos/azul.bin" "gfx/logos/azul_hdr.bin" "gfx/logos/total.bin"
                "gfx/logos/avianca.bin" "gfx/logos/wamos.bin"
                "gfx/logos/tap.bin" "gfx/logos/aal.bin" "gfx/logos/ual.bin" "gfx/logos/dal.bin"
                "gfx/logos/afr.bin" "gfx/logos/klm.bin" "gfx/logos/dlh.bin" "gfx/logos/ibe.bin"
                "gfx/logos/baw.bin" "gfx/logos/uae.bin" "gfx/logos/qtr.bin" "gfx/logos/thy.bin"
                "gfx/logos/cmp.bin" "gfx/logos/arg.bin" "gfx/logos/aca.bin" "gfx/logos/aea.bin"
                "gfx/logos/ity.bin" "gfx/logos/swr.bin" "gfx/logos/eth.bin" "gfx/logos/amx.bin"
                "gfx/logos/bov.bin" "gfx/logos/sku.bin" "gfx/logos/jat.bin"
                "tz/tz_grid.bin"
    REQUIRES board_p4 lvgl driver
             nvs_flash esp_wifi esp_event esp_netif
             esp_http_client esp-tls json esp_timer esp_mm heap
)
