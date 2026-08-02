# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES)
set(COMPONENT_PRIV_REQUIRES 
    driver 
    esp_driver_i2c
    esp_driver_spi
    heap 
    esp_rom 
    nvs_flash 
    esp_timer 
    log 
    app_update 
    esp_partition 
    bt 
    spiffs 
    u8g2 
    u8g2_hal
    mbedtls
)

set(COMPONENT_SRCS 
    "main.cpp" 
    "bt/a2dp_sink_native.cpp"
    "ota/idf_update.cpp" 
    "led/led_strip_encoder.c"
)
set(COMPONENT_ADD_INCLUDEDIRS 
    "." 
    "audio" 
    "ble" 
    "config" 
    "bt"
    "dsp" 
    "input" 
    "led" 
    "ota" 
    "storage"
    "display"
)

register_component()

# Suppress warnings for compatibility with newer GCC
target_compile_options(${COMPONENT_LIB} PRIVATE 
    -DESP32_CMAKE=1 
    -Wno-error 
    -Wno-format 
    -fpermissive 
    -mtext-section-literals
    -Wno-overloaded-virtual
)