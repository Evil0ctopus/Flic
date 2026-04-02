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

## SD card primary storage
Flic now treats the SD card as the primary runtime storage for user-modifiable assets and state:

- `/Flic/voices/*.tflite` for neural voice packs
- `/Flic/memory/*.json` for personality, emotion, and learning state
- `/Flic/config/settings.json` for persisted user settings (no reflashing required)
- `/Flic/logs/*.log` for runtime logs
- `/Flic/animations/*.json` for animation content
- `/Flic/sounds/*.wav` for audio assets

### Face surface
- `firmware/engine/face_engine.cpp` is the primary full-screen face surface.
- Face PNGs live under `/Flic/animations/face/<style>/<animation_name>/*.png`.
- `face.json` controls face style, blink speed, glow, eye color, and AI permissions.
- WebUI exposes face style selection, previews, and save/apply controls without reflashing.
- AI hooks are gated by `ai_can_modify` and `ai_can_create` in `/Flic/config/face.json`.

### Flic Prime animation blueprint (Phase 2.6)
- Required default folders are enforced at boot:
	- `/Flic/animations/face/default/blink`
	- `/Flic/animations/face/default/idle`
	- `/Flic/animations/face/default/listening`
	- `/Flic/animations/face/default/thinking`
	- `/Flic/animations/face/default/speaking`
	- `/Flic/animations/face/default/happy`
	- `/Flic/animations/face/default/sad`
	- `/Flic/animations/face/default/surprise`
- Blueprint metadata file: `/src/face/animation_metadata.json` (mirrored to `/Flic/animations/face/animation_metadata.json` on SD when missing).
- PNG frame sequences are numerically sorted (`frame_000.png`, `frame_001.png`, ...).
- Playback supports timing curves per animation: `Linear`, `EaseIn`, `EaseOut`, `EaseInOut`.
- Glow modulation rules:
	- inner radius: `12px`
	- outer radius: `28px`
	- gradient: `#FFFFFF -> #AEE6FF -> #C8B5FF`
	- opacity range: `35% - 55%`
- Speaking animation is amplitude-driven from neural TTS envelope (glow intensity + subtle eye bounce + dynamic frame timing).
- Emotion mapping:
	- `neutral -> idle`
	- `listening -> listening`
	- `thinking -> thinking`
	- `speaking -> speaking`
	- `happy -> happy`
	- `sad -> sad`
	- `surprised -> surprise`
	- `tired -> idle (slow)`
	- `excited -> idle (fast) + speaking pulse`
- Playback is non-blocking and interruptible (`speaking`/`listening` can override idle queue), with queued transitions and smooth return to `idle` after speaking.
- WebUI face endpoints:
	- `GET /api/face/animations`
	- `POST /api/face/preview`
	- `POST /api/face/set_animation`

### WebUI SD manager + animation tools (Phase 2.65)
- WebUI now includes sidebar navigation with dedicated panels:
	- `SD Card Manager`
	- `Face Animation Tools`
	- `Logs & Diagnostics`
- SD Card Manager endpoints:
	- `GET /api/sd/list?path=<path>`
	- `POST /api/sd/upload?path=<path>`
	- `POST /api/sd/delete`
	- `POST /api/sd/mkdir`
	- `POST /api/sd/rename`
	- `GET /api/sd/download?path=<path>`
- Face Animation Tools endpoints:
	- `POST /api/face/play`
	- `POST /api/face/set_emotion`
	- `POST /api/face/reload`
	- `GET /api/face/validate`
	- `GET /api/face/snapshot`
- Logs endpoint:
	- `GET /api/logs?limit=500&subsystem=<name>`
	- `POST /api/logs/clear`
- Upload validation enforces PNG frame naming (`frame_XXX.png`), `240x240`, and RGBA PNG color type.
- File operations are path-sanitized and logged to `/Flic/logs/webui_sd.log`.

The firmware creates these directories at mount time and keeps legacy `/ai/*` directories for backward compatibility.

## Build
Open the workspace in PlatformIO and use the `m5cores3` environment from `platformio.ini`.
