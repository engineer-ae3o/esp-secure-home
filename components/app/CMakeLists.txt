idf_component_register(SRCS "src/file.cpp"
                            "src/wifi.cpp"
                            "src/tasks.cpp"
                            "src/system.cpp"
                            "src/display.cpp"
                            "src/telegram.cpp"
                            "src/storage.cpp"
                       INCLUDE_DIRS "inc/"
                       REQUIRES freertos drivers esp_wifi esp_netif esp-idf-lib__i2cdev
                       esp-idf-lib__hd44780 joltwallet__littlefs esp-idf-lib__pcf8574
                       esp_event 
)
