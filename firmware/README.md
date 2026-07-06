# 🔧 Firmware — Eccles Smart Bike (ESP32 / ESP-IDF)

This folder contains everything needed to run the firmware on a real ESP32, in two forms:

```
firmware/
├── prebuilt/     ← flash these directly, no compiler needed
│   ├── bootloader.bin
│   ├── firmware.bin
│   └── littlefs.bin
└── source/       ← the full ESP-IDF project, if you want to build/modify it
```

---

## 🚀 Option A — Flash the prebuilt binaries (fastest)

**You do not need ESP-IDF, PlatformIO, or a compiler for this.** All you need is [`esptool`](https://github.com/espressif/esptool):

```sh
pip install esptool
```

Then flash all three images in one go:

```sh
esptool.py --chip esp32 --port <YOUR_PORT> --baud 460800 write_flash \
  0x1000   prebuilt/bootloader.bin \
  0x10000  prebuilt/firmware.bin \
  0x190000 prebuilt/littlefs.bin
```

- `<YOUR_PORT>` — your ESP32's serial port, e.g. `COM7` (Windows), `/dev/ttyUSB0` (Linux), `/dev/cu.usbserial-1420` (macOS)
- These offsets match the project's custom partition table (`source/partitions.csv`): bootloader at `0x1000`, the app at `0x10000`, and the LittleFS data partition (web UI + packed TTS voice model) at `0x190000`.

Once flashed, open a serial monitor at **115200 baud** to watch boot logs, or just power-cycle the board — it will start its WiFi hotspot and Bluetooth stack automatically.

### Monitoring over serial

```sh
esptool.py --chip esp32 --port <YOUR_PORT> --baud 115200 monitor
```
*(or any serial terminal of your choice, e.g. `screen`, `putty`, `minicom`)*

---

## 🛠️ Option B — Build from source

If you want to modify the firmware, use the full project in `source/`.

**Requirements:**
- [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html) (`idf.py` toolchain)
- C++17, ESP32 (xtensa-esp32-elf) toolchain — installed automatically by the ESP-IDF installer

**Build & flash:**

```sh
cd source
. $IDF_PATH/export.sh
idf.py set-target esp32
idf.py build
idf.py -p <YOUR_PORT> flash monitor
```

The LittleFS data partition (containing the packed TTS voice model and the web dashboard) is built and flashed automatically as part of `idf.py flash`, via the `littlefs_create_partition_image(... FLASH_IN_PROJECT)` call in the project's root `CMakeLists.txt` — no separate upload step required.

PlatformIO is also supported (`platformio.ini` is included), if you prefer that workflow over raw `idf.py`.

### Rebuilding the voice model

If you change the `.wav` files in `source/eccles/resources/models/`, rebuild the packed TTS bundle with the host-side **Eccles TTS Packager**:

```sh
cd source
pio run -e native
```

This regenerates `source/littlefs_image/StaticModel.eccles` and `source/components/eccles/include/StaticModel.h`. See `source/eccles/tools/ReadME` for the packager's full CLI and config file format.

---

## 🧩 Partition layout

| Partition  | Type          | Offset    | Size      | Contents                                   |
|------------|---------------|-----------|-----------|---------------------------------------------|
| `nvs`      | data / nvs    | `0x9000`  | `0x6000`  | Persistent configuration                    |
| `phy_init` | data / phy    | `0xf000`  | `0x1000`  | RF calibration data                          |
| `factory`  | app           | `0x10000` | `0x180000`| The firmware application                     |
| `littlefs` | data / spiffs | `0x190000`| `0x160000`| Web UI (`index.html`/`app.js`/`style.css`) + packed TTS voice model (`StaticModel.eccles`) |

See `source/partitions.csv` for the authoritative definitions.

---

## 📚 Technical deep-dive

For the full architecture breakdown, the complete Arduino → ESP-IDF migration table, known limitations, and the story of the DAC/I2S debugging saga, see:

➡️ **[`source/PORT_NOTES.md`](source/PORT_NOTES.md)**
