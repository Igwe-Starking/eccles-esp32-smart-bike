# 🌐 Web UI — Eccles Smart Bike

The on-device web dashboard, served directly by the ESP32 from its LittleFS partition — no app install required to use it.

```
web-ui/
├── index.html
├── app.js
└── style.css
```

## How it's used

These three files are packed into the firmware's `littlefs` partition at build/flash time (see [`../firmware/README.md`](../firmware/README.md)) and served by the ESP32's built-in HTTP server. Once the firmware is flashed:

1. Connect a device to the bike's WiFi hotspot.
2. Open the bike's IP address in any browser.
3. The dashboard loads directly from the ESP32 — devices, live sensors, and a raw command terminal, communicating over a WebSocket for real-time updates.

## Editing the dashboard

This copy of `index.html` / `app.js` / `style.css` is kept here for easy browsing and editing. The **authoritative copy used by the firmware build** lives at `../firmware/source/littlefs_image/`, since that's the exact path the build system packs into the flashed filesystem image — if you make changes here, mirror them there (or vice versa) before rebuilding the firmware.

## Screenshots

See [`../docs/images/web-ui/`](../docs/images/web-ui) for the dashboard, devices panel, sensors panel, and terminal in action.
