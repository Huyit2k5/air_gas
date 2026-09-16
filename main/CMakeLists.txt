idf_component_register(SRCS "gas_main.c" "gas_sensor.c" "gas_network.c" "gas_discord.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_event nvs_flash esp_http_client esp_adc)
