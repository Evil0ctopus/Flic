# Firmware Integration Check

## Required Runtime Order

1. Mount SD as early as possible in `setup()` via `initializeStorage()`.
2. Log SD status immediately after mount attempt with `Flic::SdDiagnostics::logSdStatus()`.
3. Continue boot in fallback mode when SD is unavailable.

## Canonical Paths

- Boot animation: `/Flic/boot/`
- Faces: `/Flic/animations/face/default/`
- Sounds: `/Flic/sounds/`

## Diagnostics Hooks

The following hooks are implemented in `firmware/diagnostics/sd_diagnostics.cpp`:

- `logSdStatus()`
- `logFaceLoad(const String& filename, const String& fullPath)`
- `logBootAnimationFrame(const String& filename)`
- `logSoundPlay(const String& filename)`

## Where Hooks Are Called

- `main.cpp`: after SD mount attempt in `initializeStorage()`.
- `animation_engine.cpp`: before opening each face JSON.
- `boot_animation.cpp`: before drawing each SD boot frame.
- `audio_output.cpp`: when tones/creature sounds are played.

## Fallback Behavior

When SD mount fails:

- Boot indicator displays `SD ERROR` and still animates a built-in fallback.
- Runtime logs fallback activation and expected SD paths.
- A minimal built-in face is rendered from `main.cpp` to avoid blank output.
- Device does not hard-crash or wait indefinitely.
