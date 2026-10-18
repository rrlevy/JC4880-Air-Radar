idf_component_register(
    SRCS "esp_lcd_touch.c" "esp_lcd_touch_gt911.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd driver
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    CONFIG_ESP_LCD_TOUCH_MAX_POINTS=5
    CONFIG_ESP_LCD_TOUCH_MAX_BUTTONS=0
)
