file(GLOB UI_FONTS "fonts/*.c")
idf_component_register(
    SRCS "main.c" "sh_model.c" "sh_network.c" "sh_cloud.c" "sh_auth.c" "sh_http.c" "sh_ai.c" "sh_calendar.c" "sh_weather_parse.c" "sh_ui.c" "sh_assets.c" ${UI_FONTS}
    INCLUDE_DIRS "."
    REQUIRES esp_timer esp_wifi esp_event esp_netif nvs_flash esp_http_client mbedtls lwip esp_driver_usb_serial_jtag
)
foreach(SCENE home weather quota detail standby)
    target_add_binary_data(${COMPONENT_LIB} "${PROJECT_DIR}/assets/backgrounds/${SCENE}.rgb565" BINARY ALIGN 4)
endforeach()
