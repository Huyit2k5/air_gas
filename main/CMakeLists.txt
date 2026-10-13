idf_component_register(SRCS "gas_buzzer.c" "gas_main.c" "gas_sensor.c" "air_sensor.c" "gas_network.c" "gas_discord.c" "gas_led.c" "gas_mqtt.c" "gas_config.c" "gas_control.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_netif esp_event esp_timer nvs_flash esp_http_client esp_adc driver mqtt json mbedtls)
