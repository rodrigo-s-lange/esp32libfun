if(CONFIG_ESP32LIBFUN_MCPWM AND CONFIG_SOC_MCPWM_SUPPORTED)
    set(srcs "esp32libfun_mcpwm.cpp")
    set(requires esp_driver_gpio esp_driver_mcpwm)
else()
    set(srcs "")
    set(requires "")
endif()

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS "include"
                    REQUIRES ${requires})
