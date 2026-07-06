# Changelog

All notable changes to this project are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/), and this project uses date-based milestones rather than strict semantic versioning, since firmware and app are versioned somewhat independently.

---

## [1.0.0] — ESP-IDF Port

### Added
- Complete, from-scratch port of the firmware from Arduino/PlatformIO to **pure ESP-IDF v5.x** — no Arduino core, no Arduino libraries.
- `PORT NOTE:` comments throughout the codebase documenting exactly what changed and why, file by file.
- Full Arduino → ESP-IDF API migration (see [`firmware/source/PORT_NOTES.md`](firmware/source/PORT_NOTES.md) for the complete table), including:
  - `digitalWrite`/`digitalRead`/`pinMode` → `driver/gpio.h`
  - `analogRead` → `esp_adc/adc_oneshot.h`
  - `analogWrite` → `driver/ledc.h`
  - `dacWrite` → `driver/dac_oneshot.h`
  - `Serial` → `driver/uart.h`
  - `millis()`/`micros()` → `esp_timer_get_time()`
  - `attachInterruptArg` → `gpio_install_isr_service`/`gpio_isr_handler_add`
  - Arduino `SPIFFS`/`LittleFS` → `esp_vfs_spiffs_register` + POSIX `FILE*`
  - Arduino `Preferences` → raw `nvs_flash`/`nvs.h`
  - `WiFi.h` → `esp_wifi.h` + `esp_netif` + `esp_event`
  - `ESPAsyncWebServer`/`AsyncWebSocket` → `esp_http_server` (native WebSocket support)
  - `WiFiUDP` → raw lwIP/BSD UDP socket
  - `ESPmDNS` → `mdns` component
  - Arduino `String` → a tiny drop-in `Eccles::String` replacement
  - `setup()`/`loop()` → `app_main()` with an explicit run loop
- Ready-to-flash prebuilt firmware binaries (`bootloader.bin`, `firmware.bin`, `littlefs.bin`) included directly in the repository.
- Ready-to-install Android APK included directly in the repository.
- Reorganized repository structure: dedicated `firmware/`, `android-app/`, `web-ui/`, and `docs/` directories, each with their own README.
- This CHANGELOG and CONTRIBUTING guide.

### Changed
- System stabilized on the audio/DAC path after the debugging effort inherited from the original prototype (see below).
- Bluetooth Classic (A2DP + AVRCP) stack, `driver/i2s.h` legacy audio path, and the host-side Eccles TTS Packager were carried over unchanged, since they were already raw ESP-IDF in the original implementation.

### Known limitations (carried over from the original)
- AVRCP absolute volume / volume up-down are stubbed out, matching the original's documented ESP-IDF ~3.3 AVRC API limitations.
- DAC output instability was observed in earlier versions of the underlying system (see below) — resolved by migrating to ESP32 Arduino core v3.5.0 in the original prototype, and now further isolated by this ESP-IDF port.

---

## [0.x] — Original Arduino/PlatformIO Prototype

The system originated as an Arduino/PlatformIO firmware, preserved for history and comparison at:
🔗 [igwe-starking/eccles-esp-arduino-smart-bike](https://github.com/igwe-starking/eccles-esp-arduino-smart-bike)

### Notable events from that phase
- Initial implementation used DAC + I2S audio streaming for on-device text-to-speech playback.
- **Critical DAC/I2S bug:** after extensive testing, DAC output became unstable, with output voltage stuck around ~0.08V, causing failed or distorted audio playback.
- **~2 weeks of hardware-level debugging**: multiple rewiring tests, pin-matrix reconfiguration experiments, DAC-vs-I2S mode switching trials, and signal verification with a multimeter. Photos of this process are preserved in [`docs/images/debugging/`](docs/images/debugging).
- System was eventually migrated and stabilized on **ESP32 Arduino core v3.5.0**, which the Arduino-based prototype originally targeted.
- Built the first versions of: the Android companion app, the on-device web dashboard, the Bluetooth Classic (A2DP/AVRCP) stack, and the Eccles TTS Packager toolchain.

---

## Future / Roadmap

Tracked in the [README roadmap](README.md#-roadmap). Highlights:
- Re-enable AVRCP absolute volume / volume up-down
- Migrate to the modern `driver/i2s_std.h` channel API
- Improved TTS synthesis quality
- Secure communication layer between the Android app and the ESP32
- Firmware signing / update protection
