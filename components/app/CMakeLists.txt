idf_component_register(SRCS "src/file.cpp"
                            "src/tasks.cpp"
                            "src/system.cpp"
                            "src/display.cpp"
                            "src/sim800l.cpp"
                            "src/storage.cpp"
                       INCLUDE_DIRS "inc/"
                       REQUIRES freertos drivers espressif__esp_modem esp-idf-lib__i2cdev
                       esp-idf-lib__hd44780 joltwallet__littlefs esp-idf-lib__pcf8574
)
