# AGENTS.md

- This repo is the app manifest repo inside a Zephyr west workspace. App code lives under `app/`; the parent workspace contains west-managed `zephyr/`, `modules/`, and `bootloader/` dependencies.
- Workspace source of truth: parent `.west/config` uses this repo's `west.yml`, which currently pins upstream Zephyr `v4.4.0`.
- Global `west` may be missing. From the parent workspace root, use `./zephyr-workspace/.venv/bin/west` or activate `zephyr-workspace/.venv/` before running west commands.
- Current implemented app is under `app/`: `CMakeLists.txt` builds `src/main.c`, `src/app_channels.c`, `src/button_input.c`, `src/bike_config.c`, `src/bike_shell.c`, `src/bike_state.c`, and `src/led_status.c`, with headers in `include/`.
- Development build from the parent workspace root: `./zephyr-workspace/.venv/bin/west build -b native_sim bikeshare-firmware/app -d build/native_sim -p always`.
- Run native simulator build from the parent workspace root: `./zephyr-workspace/.venv/bin/west build -d build/native_sim -t run`.
- Current `app/prj.conf` enables runtime-only Settings, shell, logging, zbus, and `native_sim` TAP networking with firmware IP `192.0.2.1` and gateway/host `192.0.2.2`.
- Implemented shell commands are `bike set id <ID>`, `bike set token <TOKEN>`, `bike set mqtt_host <HOST>`, `bike set mqtt_port <PORT>`, `bike set apn <APN>`, `bike get`, `bike state`, `bike sim authorize <RENTAL_ID>`, `bike sim cancel [RENTAL_ID]`, and `bike sim button`.
- `bike_state` implements the initial `UNREGISTERED`, `AVAILABLE`, `RESERVED`, and `IN_USE` transitions plus a 60-second reservation timeout. `led_status` observes state changes and maps them to LED patterns, using `led0` when available. `button_input` reads `sw0` when available and publishes debounced button events. The `ERROR` fault path, MQTT, LTE, GNSS, NVS persistence validation, LED hardware validation, and button hardware validation remain pending.
- Initial Twister/ZTEST coverage exists under `tests/` for config validation, core state transitions, LED mapping/cached init, button event publishing, and debounce filtering. Test command shape is `west twister -p native_sim/native/64 -T bikeshare-firmware/tests`.
- Final hardware target is intended to be nRF9160 DK with NCS, but the current manifest is upstream Zephyr, not NCS. Hardware docs use `nrf9160dk/nrf9160/ns`, with older fallback `nrf9160dk_nrf9160_ns`.
- Build outputs are expected under parent workspace-level `build/`; VS Code points IntelliSense at `build/native_sim/compile_commands.json`.
