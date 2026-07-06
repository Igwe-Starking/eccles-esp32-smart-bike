# 📱 Android App — Eccles Smart Bike

The Android companion app for the Eccles Smart Bike. It connects to the ESP32's WiFi hotspot or Bluetooth, gives you device controls, live sensor readouts, Bluetooth media controls, a voice conversation mode, and a raw command terminal.

```
android-app/
├── release/
│   └── EcclesSmartBike.apk   ← install this directly, no build needed
└── source/                    ← the full Android Studio project
```

---

## 🚀 Option A — Install the prebuilt APK (fastest)

No Android Studio, no Gradle, no SDK setup required.

1. Download **[`release/EcclesSmartBike.apk`](release/EcclesSmartBike.apk)** to your Android device.
2. Allow installs from unknown sources if prompted (Android will guide you through this on first install).
3. Open the app and grant the requested permissions:
   - **Microphone** — for voice commands and conversation mode
   - **WiFi / Network state** — to detect and connect to the bike's hotspot
4. Power on the bike's ESP32 and either:
   - connect your phone's WiFi to the bike's hotspot, or
   - pair over Bluetooth,

   and the app will detect the connection automatically.

**Minimum requirements:** Android 8.0 (API 26) or later, a microphone (optional but recommended for voice features).

---

## 🛠️ Option B — Build from source

If you want to modify the app, use the project in `source/`.

**Requirements:**
- Android Studio (recent stable release)
- Android SDK with a recent build-tools version (see `source/gradle/libs.versions.toml`)
- Android NDK (the app includes a native C++ layer under `app/src/main/cpp/`)

**Build & run:**

```sh
cd source
./gradlew assembleDebug
```

Or simply open the `source/` folder in Android Studio and hit **Run**. The generated APK will be under `source/app/build/outputs/apk/`.

---

## ✨ App features

- **Device controls** — headlamp, horn, left/right turn signals, ignition, starter, and engine, in one screen
- **Live sensors** — fuel level and temperature, refreshed on demand
- **Bluetooth media panel** — prev / play / pause / next / volume, driving the bike's AVRCP media controls
- **Conversation mode** — start an AI-driven voice conversation or a direct real-time voice link with the bike
- **Command terminal** — type or speak raw commands for debugging and advanced control
- **Automatic hotspot discovery** — the app watches for the bike's WiFi hotspot (`UdpDiscovery`) and connects without manual configuration

## 🧱 Architecture notes

The app pairs a Java/Kotlin UI layer with a native C++ engine (`app/src/main/cpp/eccles/`) that mirrors the firmware's own command/transport types (`Command`, `CommandFactory`, `Transport`, `EcclesTypes`), so commands generated on the phone map directly onto the same structures the ESP32 firmware understands.

## 📸 Screenshots

See [`../docs/images/android-ui/`](../docs/images/android-ui) for the full set.
