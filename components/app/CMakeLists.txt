idf_component_register(SRCS "src/tasks.cpp"
                       INCLUDE_DIRS "inc/"
                       REQUIRES freertos drivers espressif__esp_modem
                       esp-idf-lib__hd44780 joltwallet__littlefs
)
