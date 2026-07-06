# 🚲 Eccles Bike Project (ESP32 Smart Control System) — Pure ESP-IDF Port

> This is a from-scratch port of the original PlatformIO/Arduino-framework firmware to pure
> ESP-IDF (no Arduino core, no third party Arduino libraries). The architecture, module
> boundaries, class/namespace names, function names, and original comments are preserved
> throughout; only the underlying framework calls (GPIO, ADC, I2S, filesystem, bluetooth,
> wifi/http/websocket, NVS) were swapped for their native esp-idf equivalents. Every file
> that changed carries a `PORT NOTE:` comment explaining exactly what changed and why.

## 🧠 Overview

The **Eccles Bike Project** is an ESP32-based smart embedded system designed to integrate:

- Device control architecture
- Audio playback via DAC / I2S
- Custom TTS model packaging system (Eccles TTS Packager)
- Real-time command processing from an Android mobile application
- Sensor and hardware interaction layer

This project explores low-level ESP32 audio handling, real-time embedded control systems, and mobile-to-device communication.

---

## 👤 Author

**Nwobodo Ecclesiastes Chidera**
A.K.A **Igwe Starking**

---

## ⚙️ Core Features

### 🔧 Embedded System Core
- Modular device architecture using `DeviceManager`
- Runtime device abstraction layer
- Live data handling system (`LiveData`)
- GPIO + DAC + I2S integration
- Watchdog timer support for system stability

### 🔊 Audio System
- DAC-based audio output (initial implementation)
- I2S audio streaming support
- Experimental real-time audio playback system
- Custom buffer handling for audio streaming

### 🎙️ Eccles TTS Packager
A custom-built **host side, platform agnostic** toolchain (unchanged by this port, see
`eccles/tools/`) designed to:

- Pack `.wav` audio samples into a structured TTS model
- Generate compressed model binaries (`StaticModel.eccles`)
- Auto-generate `StaticModel.h` for embedded integration
- Prepare assets during the build workflow

### 📱 Android Command System
An external Android application:

- Captures user voice input
- Converts speech into structured binary/string commands
- Sends commands to ESP32 over WiFi (websocket) or Bluetooth A2DP
- Enables remote control of device functions in real time

---

## 🧱 What changed in this port

| Original (Arduino/PlatformIO)              | This port (pure ESP-IDF)                                   |
|---------------------------------------------|--------------------------------------------------------------|
| `digitalWrite`/`digitalRead`/`pinMode`      | `driver/gpio.h` (`gpio_set_level`, `gpio_set_direction`)     |
| `analogRead`                                | `esp_adc/adc_oneshot.h`                                      |
| `analogWrite`                               | `driver/ledc.h`                                               |
| `dacWrite`                                  | `driver/dac_oneshot.h`                                        |
| `Serial`                                    | `driver/uart.h` (UART_NUM_0)                                  |
| `millis()` / `micros()`                     | `esp_timer_get_time()`                                        |
| `attachInterruptArg`                        | `gpio_install_isr_service` / `gpio_isr_handler_add`            |
| `esp_task_wdt_init(t,true)` (legacy 2-arg)  | `esp_task_wdt_config_t` + `esp_task_wdt_init()` (idf 5.x)      |
| Arduino `SPIFFS`/`LittleFS` + `File`        | `esp_vfs_spiffs_register` + POSIX `FILE*`                      |
| `driver/i2s.h` (legacy I2S)                 | unchanged — already raw esp-idf in the original                |
| Bluedroid Classic BT/A2DP/AVRCP             | unchanged — already raw esp-idf in the original                |
| Arduino `Preferences`                        | raw `nvs_flash`/`nvs.h`                                        |
| `WiFi.h`                                    | `esp_wifi.h` + `esp_netif` + `esp_event`                        |
| `ESPAsyncWebServer` / `AsyncWebSocket`      | `esp_http_server` (native websocket support)                   |
| `WiFiUDP`                                   | raw lwIP/BSD UDP socket                                         |
| `ESPmDNS`                                   | `mdns` component                                                |
| Arduino `String`                            | `Eccles::String` (tiny drop-in `e_stringa` replacement)         |
| `setup()` / `loop()` (called forever by the Arduino runtime) | `app_main()` (`eccles_main`) with an explicit `while(1){ eccles_thread(); }` |

The **eccles-tts-packager** host tool (`eccles/tools/`) is pure, platform agnostic C++ with
zero Arduino/ESP-IDF dependency — it was left completely untouched.

---

## 🧱 System Architecture

```
components/eccles/
  include/   - all headers (EcclesTypes, Pins, HardwareDevice, Devices, DeviceManager,
               FileSystem, Audio, RuntimeMemory, Executor, Executors, EcclesTTS,
               Transport, StaticModel, Eccles.h umbrella header)
  src/       - matching implementation files
main/
  main.cpp   - app_main()/eccles_main(), wires up the ExecutorManager + Transport + TTS
eccles/
  tools/     - the unchanged host-side TTS packager (native C++, its own build)
  resources/ - source .wav files the packager consumes
spiffs_image/
  StaticModel.eccles - the packaged TTS audio bundle, flashed to the "spiffs" partition
partitions.csv        - nvs + factory app + spiffs data partition layout
sdkconfig.defaults     - classic bluetooth, custom partition table, watchdog, stack sizing
```

---

## ⚠️ Major Development Challenges (carried over from the original)

### 🔴 DAC + I2S Hardware Issue (Critical Debugging Phase)

- Initial implementation used **DAC + I2S audio streaming**
- After extensive testing, the DAC output became unstable
- Output voltage got stuck around **~0.08V**
- Audio playback failed or became distorted

### 🧪 Debugging Process

The issue required multiple hardware rewiring tests, pin-matrix reconfiguration experiments,
DAC vs I2S mode switching trials, signal verification with a multimeter, and deep ESP32
peripheral investigation.

### ⏳ Resolution

After approximately **2 weeks of debugging**, the system was migrated and stabilized on
**ESP32 Arduino core v3.5.0** (the platform this firmware originally targeted, hence the
Bluetooth executor's documented IDF ~3.3 AVRCP limitations, preserved as-is in this port).

---

## 🧠 Key Technical Concepts Used

- ESP32 Dual-core task management
- FreeRTOS task scheduling
- Memory buffer streaming for audio
- Hardware abstraction layer design (now pure esp-idf instead of Arduino)
- Binary + string command parsing system
- Embedded C++ polymorphism and casting
- Android-to-embedded communication pipeline

---

## 🚀 Build Environment

- **ESP-IDF v5.x** (`idf.py` toolchain)
- C++17
- ESP32 toolchain (xtensa-esp32-elf)
- Android mobile client (custom built, unaffected by this port)

### Building

```sh
. $IDF_PATH/export.sh
idf.py set-target esp32
idf.py build
idf.py -p <PORT> flash monitor
```

The `spiffs` partition (containing `StaticModel.eccles`) is built and flashed automatically
as part of `idf.py flash`, via the `spiffs_create_partition_image(... FLASH_IN_PROJECT)` call
in the root `CMakeLists.txt`.

### Rebuilding the TTS model bundle

The packager itself is unchanged — build it natively (see `eccles/tools/`) and re-run it
against `eccles/resources/models/*.wav` to regenerate `spiffs_image/StaticModel.eccles` and
`components/eccles/include/StaticModel.h` whenever the voice clips change.

---

## 🛑 Known Limitations

- DAC output instability observed in earlier versions (unrelated to this port)
- AVRCP absolute volume / volume up/down are stubbed out, matching the original's documented
  IDF ~3.3 AVRC API limitations — these can be re-enabled if the bluedroid AVRC API used here
  is upgraded, see the `STUBBED:` comments in `Bluetooth.cpp`
- Tight coupling between modules may require refactoring for large-scale deployment

---

## 🔮 Future Improvements

- Re-enable AVRCP absolute volume / vol up/down now that a current esp-idf AVRC API is in reach
- Improved TTS synthesis quality
- Stable I2S DMA audio streaming system (consider migrating off the legacy `driver/i2s.h` to
  the newer `driver/i2s_std.h` channel API)
- Stronger modular architecture separation
- Secure communication layer between Android and ESP32
- Firmware signing and protection system

---

## 📌 Summary

The Eccles Bike Project represents a deep exploration into embedded audio systems, real-time
ESP32 control, custom TTS tooling, and mobile-to-hardware integration. This port demonstrates
the same system running on pure ESP-IDF with the original architecture, behavior, and intent
fully intact.

---

## ⚡ License

MIT License

---
