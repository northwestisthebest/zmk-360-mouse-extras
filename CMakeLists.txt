zephyr_library()

zephyr_library_sources_ifdef(CONFIG_ZMK_LED_INDICATOR src/led_indicator.c)

zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
