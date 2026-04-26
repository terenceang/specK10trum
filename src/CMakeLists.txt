set(COMPONENT_REQS
    freertos
    esp_timer
    spi_flash
    esp_psram
    nvs_flash
    esp_event
    esp_netif
    esp_wifi
    esp_http_server
)

if(CONFIG_BT_ENABLED)
    list(APPEND COMPONENT_REQS bt wifi_provisioning protocomm)
endif()

idf_component_register(SRCS
    "main.cpp"
    "spectrum/SpectrumBase.cpp"
    "spectrum/Beeper.cpp"
    "spectrum/PSG.cpp"
    "spectrum/Snapshot.cpp"
    "spectrum/Tape.cpp"
    "spectrum/Spectrum48K.cpp"
    "spectrum/Spectrum128K.cpp"
    "z80/z80.c"
    "display/Display.cpp"
    "audio/Audio.cpp"
    "expander/Expander.cpp"
    "input/Input.cpp"
    "wifi_prov/wifi_prov.cpp"
    "webserver/Webserver.cpp"
    "instrumentation/Instrumentation.cpp"
    "../test/test_runner.cpp"
    "../test/test_memory.cpp"
    "../test/test_128k_banking.cpp"
    "../test/z80_test.c"
    REQUIRES ${COMPONENT_REQS}
)
