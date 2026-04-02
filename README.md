# Flic

Flic is a minimal CoreS3-first firmware scaffold with AI-owned behavior isolated under `ai/`.

## Layout
- `firmware/` contains device startup, engine, subsystems, and UI boot code.
- `ai/` contains dynamic behavior, scripts, animations, and memory artifacts.
- `docs/` contains project philosophy and supporting documentation.

## Runtime ownership decisions
- WebUI runtime source of truth is embedded in `firmware/engine/webui_engine.cpp` (`WebUiEngine::handleRoot()`).
- `webui/` files are reference/archive only and are not served by device firmware.
- Active speech pipeline is `AsrEngine` + WebUI command flow.
- Legacy `SpeechRecognition` module is archived under `archive/legacy/` and excluded from firmware build in `platformio.ini` via `build_src_filter`.

## Build
Open the workspace in PlatformIO and use the `m5cores3` environment from `platformio.ini`.
