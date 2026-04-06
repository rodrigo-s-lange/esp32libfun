if(IDF_TARGET STREQUAL "esp32" OR IDF_TARGET STREQUAL "esp32p4")
    set(srcs "esp32libfun_lan8720.cpp")
    set(requires esp_eth esp_event esp_netif esp_driver_gpio lan87xx)
    set(priv_requires log)
else()
    set(srcs "")
    set(requires "")
    set(priv_requires "")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES ${requires}
    PRIV_REQUIRES ${priv_requires}
)

