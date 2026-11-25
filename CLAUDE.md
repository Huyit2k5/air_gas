# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An ESP32 (ESP-IDF v5.5.x, target `esp32`, DOIT DevKit V1) gas-leak and air-quality station for a university IoT course. MQ-2 (gas) and MQ-135 (air) on ADC1, with local actuators (alarm LED, buzzer, fan via MOSFET, servo gas valve). Data goes ESP32 → MQTT (Mosquitto) → Node-RED → Home Assistant, and an AI layer (MCP server + Discord bot) reads and controls it through HA.

The user writes Vietnamese, casually. Reply in Vietnamese, short and concrete. Docs (`README.md`, `HUONG_DAN.md`, `ai-agent/README.md`) are Vietnamese. `HANDOFF.md` is a session-state note for the user's own workflow (hardware state, pending questions, credentials locations): read it for context, but do not commit it.

## Build, flash, run

ESP-IDF lives in `D:\Program\Espressif` (not the default location). From PowerShell:

```
$env:IDF_TOOLS_PATH='D:\Program\Espressif'; cmd /c 'call D:\Program\Espressif\frameworks\esp-idf-v5.5.5\export.bat && idf.py build && idf.py -p COM7 flash'
```

- Board is on **COM7** (CH340). Ask the user before flashing.
- Configuration is `idf.py menuconfig` → "Gas Monitor" (`main/Kconfig.projbuild`). Defaults for the whole project are in `sdkconfig.defaults`; `partitions.csv` is a custom 4 MB table.
- There are no firmware unit tests. Verify on hardware: read serial (115200, DTR/RTS false; the firmware logs little after boot), or watch MQTT.
- Wokwi simulation: build with `idf.py -B build_wokwi -D SDKCONFIG=build_wokwi/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.wokwi" build`, then start the simulator in VS Code (`wokwi.toml`, `diagram.json`).
- `idf.py erase-flash` wipes NVS, including sensor calibration (`gas_cal`, `aq_cal`) and saved Wi-Fi/broker/webhook. Prefer the `{"recalibrate":true}` command.

### Docker stack (`node-red/`)

`cd node-red && docker compose up -d`. Services: mosquitto (1883/9001), node-red (1880), homeassistant (8123), airgas-mcp (127.0.0.1:8001), airgas-bot. **Never rename the compose project (`name: node-red`)**: the named volumes would be orphaned.

Deploy Node-RED flows (admin API is auth-protected, so copy the file):
```
MSYS_NO_PATHCONV=1 docker cp node-red/flows.json node-red:/data/flows.json && docker restart node-red
```
Edit `flows.json` programmatically (Python `json.dumps(indent=2, ensure_ascii=False)`, no trailing newline) rather than by hand. If env vars in compose changed, `docker compose up -d node-red` first.

MQTT debugging from Git Bash: `MSYS_NO_PATHCONV=1 docker exec mosquitto mosquitto_sub -R -t 'airgas/#' -v` (`-R` skips retained). Start the subscriber before publishing a command or the ack is missed.

### AI agent (`ai-agent/`)

Python, separate venv (`ai-agent/.venv`, `requirements*.txt`). Secrets are in `ai-agent/.env` (see `.env.example`).
- `python test_mcp.py`: starts the MCP server, lists tools, calls the read-only ones (needs HA running).
- `python call_tool.py control_device action=test_buzzer`: call one tool.
- `python agent.py`: terminal chat with the LLM + tool loop. `python discord_bot.py`: the Discord bot.
- After code changes in Docker: `docker compose up -d --build airgas-mcp airgas-bot` (from `node-red/`).

## Architecture

### Firmware (`main/`)

`gas_main.c:app_main` init order matters: `gas_sensor_init()` (MQ-2) creates the shared ADC1 unit and must run before `air_sensor_init()` (MQ-135). Wi-Fi starts 1.5 s after sensors so heater + radio current peaks don't brown out a weak supply.

Everything runs in one polling loop in `app_main` (period `CONFIG_GAS_CHECK_PERIOD_MS`); it reads both sensors, decides alarms, and drives all actuators itself. Other modules are thin drivers:
- `gas_sensor.c` / `air_sensor.c`: ADC read → Rs → ppm via Rs/Ro curve; Ro auto-calibrated at first boot and stored in NVS. A reading under ~300 mV sets `fault`, and the loop goes fail-safe (valve closed, fan 100 %).
- `gas_mqtt.c`: publishes `airgas/<device>/{reading,alarm,status,ack,info}` and `airquality/<device>/reading`; receives commands on `airgas/<device>/cmd` in the MQTT task.
- `gas_control.c`: command parser (schema is documented in `gas_control.h`). It does not act directly. It stores pending state (`take_valve_request`, `take_recalibrate`, `fan_test`, `buzzer_test`, simulation mode with expiry) that the main loop polls. Keep that pattern: actuators are only touched from the main loop.
- `gas_config.c`: runtime-changeable Wi-Fi/broker/webhook in NVS, overriding Kconfig values; includes a Wi-Fi trial/revert mechanism.
- `gas_valve.c` (servo), `gas_fan.c` (PWM through MOSFET), `gas_buzzer.c`, `gas_led.c`, `gas_discord.c` (ESP32 posts Discord alerts itself), `gas_network.c`.

Behavioral rules encoded in the loop: the valve **latches closed** on gas alarm and only reopens by an explicit `open` command, which is refused during an alarm. A gas alarm beats a fan test. A simulation (`{"sim":...}`) reports fake values and only moves actuators/sends Discord when its flags allow, but the buzzer always follows `alarm`.

`main/CMakeLists.txt` lists source files explicitly (some twice, harmless). Add new `.c` files there.

### Pipeline

`flows.json` has four tabs: monitoring, emergency, dashboard/confirmation, Home Assistant & log. The central function "Gộp trạng thái & phát hiện chuyển trạng thái" emits `msg.event` (`danger|safe|poor|fault|offline|valve`) with a one-line Vietnamese text; a 30 s cooldown node feeds Discord, CSV log, and a signed webhook to the user's separate Hermes Agent (`host.docker.internal:8644`). Home Assistant entities are created by **MQTT discovery from Node-RED**, not from `homeassistant/config/configuration.yaml` (that static YAML is the older approach described in README).

`ai-agent/airgas_mcp.py` talks only to HA's REST API and finds entities by their **display name** (as set by Node-RED discovery), not `entity_id`. `control_device` presses HA buttons that Node-RED turns back into MQTT commands. The same refusal rules (no `open_valve` during alarm) exist in the MCP server and in firmware. HTTP transport requires `MCP_AUTH_TOKEN`.

`dashboard/air_gas_live.html` is a standalone live page (MQTT over websockets 9001) with a 3D model. The user asked not to move the 3D objects any more.

## Gotchas

- **README.md is partly stale.** It describes GPIO36/GPIO39 inputs and 100 % divider; the real wiring is MQ-2 AO → D35 and MQ-135 AO → D34 through 10k/10k dividers (`CONFIG_GAS_ADC_DIVIDER_PERCENT=50`, `CONFIG_AQ_ADC_DIVIDER_PERCENT=50`), buzzer D4, fan MOSFET trigger D18, servo D13, LED GPIO2. The Kconfig defaults (e.g. `GAS_ADC_PIN` 36) are not the deployed pins either: the real ones live in the local `sdkconfig`, so check that and `HANDOFF.md` before trusting README or Kconfig.
- **Wi-Fi / broker:** the broker address is baked into `sdkconfig` as the PC's IP on the "AT21" hotspot; the PC and board must share that network or the device looks offline.
- **Hardware power:** sensors, fan and servo run from a separate 5 V adapter; ESP32 runs from USB only, with VIN and 3V3 left empty and grounds common. Powering MQ sensors from VIN caused brownout loops. Floating D34/D35 shows as a sawtooth in the `mv` series.
- **Secrets:** local `sdkconfig` contains the Wi-Fi password and Discord webhook, so blank them before staging it (or leave it out). Also never commit `node-red/.env`, `ai-agent/.env`, or Home Assistant runtime data (`node-red/homeassistant/config/.storage/*`, `*.db*`, logs, `.cache`). An old webhook is already in git history. Commit and push only when asked.
- Known unresolved bugs: false "offline" alert on every Node-RED restart; `danger` text always claims valve/fan/buzzer acted even for `actuators:false` sims; the "1 điểm / 5 s" Node-RED function logs repeated "Invalid property expression" errors.
