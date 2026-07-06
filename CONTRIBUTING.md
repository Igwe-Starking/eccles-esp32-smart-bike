# Contributing to Eccles Smart Bike

First off — thank you for considering contributing. This project spans embedded firmware, a native Android app, and an on-device web UI, so contributions of very different shapes are all welcome: bug reports, hardware notes, documentation fixes, UI polish, or firmware features.

## Ways to contribute

- **🐛 Bug reports** — especially anything related to the Bluetooth AVRCP stack, DAC/I2S audio, or WiFi hotspot discovery, since those are the most hardware-sensitive parts of the system.
- **📝 Documentation** — clarifications, corrections, or added detail to any of the READMEs are always welcome.
- **🔧 Firmware improvements** — see the [roadmap](README.md#-roadmap) for known gaps (AVRCP absolute volume, the I2S driver migration, TTS quality, etc.).
- **📱 Android app improvements** — UI polish, additional device support, more robust hotspot discovery.
- **🌐 Web UI improvements** — the dashboard is plain HTML/CSS/JS with no build step, so it's an easy place to start.

## Before you start

1. **Check open issues** first to avoid duplicate work.
2. **For anything non-trivial, open an issue before submitting a PR** — a quick discussion up front saves rework later, especially for firmware changes that touch hardware timing or pin assignments.
3. **Read the relevant module's docs:**
   - Firmware architecture and the Arduino → ESP-IDF migration: [`firmware/source/PORT_NOTES.md`](firmware/source/PORT_NOTES.md)
   - Android app structure: [`android-app/README.md`](android-app/README.md)
   - Web UI: [`web-ui/README.md`](web-ui/README.md)

## Development setup

- **Firmware:** ESP-IDF v5.x, C++17. See [`firmware/README.md`](firmware/README.md) for the full build/flash workflow.
- **Android app:** Android Studio, Android SDK + NDK. See [`android-app/README.md`](android-app/README.md).
- **Web UI:** No tooling required — edit the files in `web-ui/` directly (and mirror changes into `firmware/source/littlefs_image/`, which is the copy the firmware build actually packs).

## Code style

- **C++ (firmware & Android native layer):** match the existing style in the file you're editing — this codebase preserves its original module boundaries, class names, and comment style intentionally, including `PORT NOTE:` comments documenting Arduino → ESP-IDF changes. Please follow that same convention for any new framework-level changes.
- **Java/Kotlin (Android app):** standard Android conventions, consistent with the existing `starking.eccles.smartbike` package.
- **JS/HTML/CSS (web UI):** keep it dependency-free — no frameworks, no build step.

## Submitting a pull request

1. Fork the repo and create a branch from `main`.
2. Make your changes, keeping commits focused and descriptive.
3. Test on real hardware where possible (firmware and Bluetooth/WiFi behavior can't be fully validated in simulation).
4. Update relevant documentation (README, PORT_NOTES, CHANGELOG) alongside your change.
5. Open a PR describing **what changed and why**, and note any hardware you tested on.

## Reporting bugs

Please include:
- What you expected vs. what happened
- Firmware version / commit, and whether you used the prebuilt binaries or built from source
- Serial monitor logs, if the issue is firmware-side
- Android app version and Android OS version, if the issue is app-side
- Photos of your wiring, if the issue could be hardware-related

## A note on hardware-related changes

Because this project drives real actuators on a real vehicle (ignition, starter, horn, lights), please be extra careful and explicit in PRs that touch `Pins.h`, `DeviceManager`, or `Executor`/`Executors` — include your reasoning and, ideally, test notes from real hardware.

Thanks again for helping improve the project 🙌
