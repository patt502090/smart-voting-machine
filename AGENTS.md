# Repository Guidelines

## Project Structure & Module Organization
- Firmware (Arduino/ESP32): `Arduinocommunicate/`, `ESPcommunicate/`, `esp32withRFID/`, `Patt/**/`, `music/`; each folder contains a single sketch (`<folder>.ino`).
- Dashboards (Python/Streamlit): `dashboard.py`, `dashboard2.py` (serial reader + live tally).
- Assets: `asset/` (images). Reference prototypes: `tmp/arduino/`, `tmp/esp32/`, docs in `tmp/docs/`.
- Docs: `README.md` (overview), project PDF.

## Build, Test, and Development Commands
Firmware (using Arduino CLI; install cores first):
```bash
# UNO (adjust port):
arduino-cli compile --fqbn arduino:avr:uno Arduinocommunicate
arduino-cli upload  -p /dev/tty.usbmodemXXXX --fqbn arduino:avr:uno Arduinocommunicate

# ESP32 (generic module; adjust FQBN/port):
arduino-cli compile --fqbn esp32:esp32:esp32 ESPcommunicate
arduino-cli upload  -p /dev/tty.usbserialXXXX --fqbn esp32:esp32:esp32 ESPcommunicate
```
Dashboards (local run; required libs):
```bash
pip install streamlit pandas pyserial pillow
streamlit run dashboard2.py   # modern UI
# or
streamlit run dashboard.py    # simple UI
```

## Coding Style & Naming Conventions
- Arduino/ESP32: 2-space indent; keep main sketch name matching its folder (Arduino requirement). Use `CamelCase` for types, `lower_snake_case` for functions/variables; prefer small, single-purpose functions.
- Python: 4-space indent; line length ≤ 100. Prefer `snake_case` for names. If formatting, use `black` and lint with `ruff` (optional; not enforced here).
- Filenames: avoid renames that break Arduino’s folder↔file rule.

## Testing Guidelines
- Firmware: hardware-in-the-loop. Verify serial output format `CF:X` (X=0–9). Use Arduino Serial Monitor or the dashboards to validate.
- Python: manual run via Streamlit. When adding tests, use `pytest` with `tests/test_*.py`. Aim for coverage on serial parsing and tally logic.

## Commit & Pull Request Guidelines
- History shows short, imperative commits (e.g., “add esp32”, “keypad update”). Keep messages concise; prefer “type: scope — summary” when possible (e.g., `feat: esp32 serial handshake`).
- PRs: include purpose, affected folders, test/validation notes (screenshots for dashboards), and flashing steps if firmware changes.

## Security & Configuration Tips
- Do NOT commit secrets (Wi‑Fi SSIDs/passwords, tokens). Move them to `secrets.h`/`secrets.py` and add to `.gitignore`; reference via `#include "secrets.h"` or environment variables in Python.

## Agent-Specific Instructions
- Keep changes minimal and scoped; do not restructure folders or rename sketch files.
- Prefer `rg` for search; read files in ≤250‑line chunks.
- Before adding tooling, check it’s needed and doesn’t break offline workflows.
