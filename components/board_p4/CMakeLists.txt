idf_component_register(
    SRCS "board_p4.c"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd driver esp_mm esp_driver_ppa esp_lcd_touch_gt911
)
