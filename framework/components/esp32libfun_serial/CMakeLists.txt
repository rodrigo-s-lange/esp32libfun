idf_component_register(SRCS "esp32libfun_serial.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_uart esp_driver_usb_serial_jtag)
