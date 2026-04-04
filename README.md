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

## SD Asset Pipeline

The Flic firmware includes a complete, deterministic asset pipeline for animation and face PNGs.

### Importing assets from old SD card (Drive D:)
- Run `python scripts/import_from_old_sd.py` to copy all boot and face PNGs from `D:/Flic/boot/` and `D:/Flic/animations/face/default/` into the repo under `firmware/assets/boot/` and `firmware/assets/faces/default/`.
- Filenames are preserved and existing files are overwritten.

### Storing assets in the repo
- All animation and face PNGs are stored permanently in `firmware/assets/boot/` and `firmware/assets/faces/default/`.
- These assets are version-controlled and always available for deployment.

### Uploading assets to CoreS3 SD via COM9
- Use the VS Code task **Upload Animations to CoreS3 SD** or run `python scripts/upload_assets_to_cores3.py`.
- The uploader connects to the device over COM9 at 115200 baud and uploads all PNGs in 4096-byte chunks.
- Each file is sent with CRC32 verification and always overwrites the target on the SD card.
- Directories are auto-created as needed.
- Progress is shown for each file and a summary is printed at the end.

### Chunked uploader protocol
- The firmware implements a serial command handler that accepts `BEGIN_UPLOAD`, `PATH`, `SIZE`, `CRC`, `CHUNK`, and `END_UPLOAD` commands.
- Files are transferred in binary chunks with CRC32 verification.
- The device responds with `OK` or `ERR` messages for each step.


## Face Animation Pipeline (Base Face from Boot Frame 075)

This system provides a fully deterministic, hands-off pipeline for generating and deploying face animations, using the final boot animation frame as the canonical base face.

### Boot animation import logic
- If `firmware/assets/boot/` is empty, run `python scripts/import_from_old_sd.py` to import all PNGs from `D:/Flic/boot/`.
- Filenames are preserved and existing files are overwritten.

### Base face extraction from frame_075.png
- The canonical base face is extracted from `firmware/assets/boot/frame_075.png` and copied to `firmware/assets/base_face/base.png`.
- This image is used as the reference for all face animation generation.

### ComfyUI face animation generation
- The script `scripts/generate_face_animations.py` loads `base_face/base.png` and all JSON animation definitions in `firmware/assets/faces/default/`.
- For each animation, if the PNG frames are missing, ComfyUI is invoked to generate the frames using the base face as reference and the animation JSON for emotion-specific geometry.
- The ComfyUI prompt is defined in `comfyui/face_animation_prompt.txt` and enforces strict style, color, and geometry consistency.

### Regenerating animations
- To regenerate all missing face animations, run the VS Code task **Upload All Face Animations to CoreS3 SD** or execute `python scripts/generate_face_animations.py`.
- The system only generates frames for animations that do not already exist.

### Uploading to CoreS3 SD card
- The VS Code task **Upload All Face Animations to CoreS3 SD** runs both the animation generator and the serial SD uploader.
- All assets are uploaded to the device SD card over COM9, with chunked transfer, CRC verification, and auto-created directories.
- The process is fully automated and always overwrites existing files for consistency.

### Deterministic, hands-off workflow
- All steps are automated and reproducible from the repo.
- The pipeline ensures that the device SD card always matches the repo asset vault and generated animations.
- No manual intervention is required after initial setup.

## Build
Open the workspace in PlatformIO and use the `m5cores3` environment from `platformio.ini`.

## Recovery

For full SD/device recovery and validation, run the VS Code task `Flic: Full System & SD Recovery`.

Quick asset-only reinstall is available via `Flic: Quick SD Reinstall (Assets Only)`.

Detailed steps and expected Serial diagnostics are documented in `RECOVERY.md`.
