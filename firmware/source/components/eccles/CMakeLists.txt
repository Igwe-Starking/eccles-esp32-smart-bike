# the "eccles" component: every ported source file (devices, audio, filesystem, executors,
# bluetooth, transport, tts) lives here, this is the esp-idf equivalent of the original
# project's src/+include/ directories combined into one buildable unit.
#
# REQUIRES lists every esp-idf built in component this code calls into directly:
#   driver        - gpio, ledc, uart, i2s_std, dac_continuous
#   esp_adc       - adc_oneshot (Pins/EcclesTypes analog reads)
#   esp_timer     - eccles_millis/eccles_waitMicro
#   esp_ringbuf   - pulled in transitively by some drivers, declared for safety
#   esp_littlefs  - FileSystem's esp_vfs_littlefs backing store
#   nvs_flash     - Configuration's persisted key/value settings
#   esp_wifi      - Transport's station mode wifi connection
#   esp_netif     - ip address / netif queries used by Transport
#   esp_event     - the default event loop wifi/ip events are dispatched through
#   esp_http_server - Transport's websocket + plain http server (replaces ESPAsyncWebServer)
#   mdns          - Transport's "eccles.local" hostname advertisement
#   lwip          - raw UDP broadcast socket used by Transport::broadcastIP
#   bt            - classic bluetooth (bluedroid) a2dp sink + avrcp controller used by Bluetooth.cpp
#   esp_common / freertos - implicitly required by virtually everything, declared for clarity

idf_component_register(
    SRCS
        "src/EcclesTypes.cpp"
        "src/Pins.cpp"
        "src/HardwareDevice.cpp"
        "src/Devices.cpp"
        "src/DeviceManager.cpp"
        "src/FileSystem.cpp"
        "src/Audio.cpp"
        "src/RuntimeMemory.cpp"
        "src/Executor.cpp"
        "src/ExecutorManager.cpp"
        "src/DeviceExecutor.cpp"
        "src/Configuration.cpp"
        "src/Bluetooth.cpp"
        "src/Conversation.cpp"
        "src/EcclesTTS.cpp"
        "src/Transport.cpp"
        "src/PowerManager.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
        esp_adc
        esp_timer
        esp_ringbuf
        esp_littlefs
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_http_server
        mdns
        lwip
        bt
        esp_pm
        esp_common
        freertos
        esp_system
)
