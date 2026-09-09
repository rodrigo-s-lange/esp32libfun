idf_component_register(SRCS "main.cpp" "test_scene.cpp"
    "${BENCH_ROOT}/main/scene_codec.cpp" "${BENCH_ROOT}/main/scene_store.cpp"
    "${COMPOSER_ROOT}/test/test_composer.cpp" "${COMPOSER_ROOT}/test/test_composer_at.cpp"
    INCLUDE_DIRS "." "${BENCH_ROOT}/main" "${COMPOSER_ROOT}/test"
    REQUIRES esp_pixels_composer esp_pixels_fx esp32libfun_at esp32libfun_serial nvs_flash)
# Fault injection around the actual store implementation, without touching NVS.
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=nvs_flash_init,--wrap=nvs_open,--wrap=nvs_get_blob,--wrap=nvs_set_blob,--wrap=nvs_commit")
