# REQUIRES is resolved by ESP-IDF in an early pass, before Kconfig is loaded,
# so it must NEVER be conditioned on a CONFIG_* value (a lesson already learned
# the hard way for esp32libfun_at) -- doing so silently drops the requirement,
# leaving the later compile of the real .cpp without its include paths. Only
# SRCS (evaluated later, once Kconfig is available) is safe to condition.
if(CONFIG_ESP32LIBFUN_MCPWM)
    set(esp32libfun_mcpwm_srcs "esp32libfun_mcpwm.cpp")
else()
    # Kconfig already hides this option via "depends on SOC_MCPWM_SUPPORTED",
    # but ESP-IDF still builds every discovered component regardless of that
    # flag. Compile nothing instead of the real .cpp on chips without an MCPWM
    # peripheral (e.g. ESP32-C3/C6), which reference HAL enums that only exist
    # for chips with the hardware.
    set(esp32libfun_mcpwm_srcs "")
endif()

idf_component_register(SRCS ${esp32libfun_mcpwm_srcs}
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_gpio esp_driver_mcpwm)
