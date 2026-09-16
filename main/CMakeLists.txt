idf_component_register(SRCS "gas_main.c" "gas_sensor.c" "gas_network.c" "gas_discord.c" "gas_led.c" "gas_mqtt.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_event nvs_flash esp_http_client esp_adc driver mqtt)
