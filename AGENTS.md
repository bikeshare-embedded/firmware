# AGENTS.md

- This repo is the app manifest repo inside a Zephyr west workspace. App code lives under `app/`; the parent workspace contains west-managed `zephyr/`, `modules/`, and `bootloader/` dependencies.
- Workspace source of truth: parent `.west/config` uses this repo's `west.yml`, which currently pins upstream Zephyr `v4.4.0`.
- Global `west` may be missing. From the parent workspace root, use `./zephyr-workspace/.venv/bin/west` or activate `zephyr-workspace/.venv/` before running west commands.
- Current implemented app is under `app/`: `CMakeLists.txt` builds `src/main.c` and `src/bike_config.c`, with headers in `include/`.
- Development build from the parent workspace root: `./zephyr-workspace/.venv/bin/west build -b native_sim bikeshare-firmware/app -d build/native_sim -p always`.
- Run native simulator build from the parent workspace root: `./zephyr-workspace/.venv/bin/west build -d build/native_sim -t run`.
- Current `app/prj.conf` enables runtime-only Settings, shell, logging, and `native_sim` TAP networking with firmware IP `192.0.2.1` and gateway/host `192.0.2.2`.
- Implemented shell commands are `bike set id <ID>`, `bike set token <TOKEN>`, `bike set url <URL>`, `bike get`, and `bike test`.
- `bike test` is an HTTP connectivity check using the configured URL; it sends `GET /health` and depends on `CONFIG_NETWORKING=y`.
- Docs mention planned MQTT/APN/simulation commands, zbus, button/LED, LTE/GNSS, and Twister tests; verify source before assuming those exist.
- No `tests/` directory exists yet. Planned test command shape is `west twister -p native_sim -T bikeshare-firmware/tests` once tests are added.
- Final hardware target is intended to be nRF9160 DK with NCS, but the current manifest is upstream Zephyr, not NCS. Hardware docs use `nrf9160dk/nrf9160/ns`, with older fallback `nrf9160dk_nrf9160_ns`.
- Build outputs are expected under parent workspace-level `build/`; VS Code points IntelliSense at `build/native_sim/compile_commands.json`.
