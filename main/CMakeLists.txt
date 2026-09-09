idf_component_register(
    SRCS "main.cpp" "scene_codec.cpp" "scene_store.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp32libfun_at esp32libfun_serial esp_pixels esp_pixels_composer adapter_esp_pixels log nvs_flash
)
