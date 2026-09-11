idf_component_register(SRCS "main.cpp" "nvs_mock.cpp" "test_scene.cpp" "test_presets.cpp"
    "${BENCH_ROOT}/main/scene_codec.cpp" "${BENCH_ROOT}/main/scene_store.cpp"
    "${BENCH_ROOT}/main/preset_codec.cpp" "${BENCH_ROOT}/main/preset_store.cpp" "${BENCH_ROOT}/main/presets_at.cpp"
    "${COMPOSER_ROOT}/test/test_composer.cpp" "${COMPOSER_ROOT}/test/test_composer_at.cpp"
    INCLUDE_DIRS "." "${BENCH_ROOT}/main" "${COMPOSER_ROOT}/test"
    REQUIRES esp_pixels_composer esp_pixels_fx esp32libfun_at esp32libfun_serial nvs_flash)
# Fault injection around the actual store implementations (SceneStore,
# PresetStore), without touching real NVS -- see nvs_mock.hpp/.cpp.
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=nvs_flash_init,--wrap=nvs_open,--wrap=nvs_flash_init_partition,--wrap=nvs_open_from_partition,--wrap=nvs_get_blob,--wrap=nvs_set_blob,--wrap=nvs_commit,--wrap=nvs_erase_key")
