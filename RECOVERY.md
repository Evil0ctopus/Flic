# Flic Recovery Guide

## Run Recovery

Use one of these VS Code tasks:

- `Flic: Full System & SD Recovery`
- `Flic: Quick SD Reinstall (Assets Only)`

Full recovery performs:

1. SD detection
2. FAT32 validation
3. Required folder creation under `/Flic`
4. Sweep/cleanup with optional unknown-file archive
5. Reference asset install and validation

Quick reinstall skips the sweep/archive step and reinstalls assets only.

## Serial Logs To Expect

After firmware boot you should see logs similar to:

- `[SD] mounted=true|false`
- `[SD] boot_path=/Flic/boot/`
- `[SD] face_path=/Flic/animations/face/default/`
- `[SD] sound_path=/Flic/sounds/`
- `[SD][FACE] file=... path=...`
- `[SD][BOOT] frame=...`
- `[SD][SOUND] play=...`

If SD is missing/unreadable:

- `[SD] fallback mode active...`
- Boot shows `SD ERROR`
- Minimal built-in face is rendered

## Confirm Working State

1. Boot animation plays from `/Flic/boot` when SD is healthy.
2. Face JSON loads print exact `/Flic/animations/face/default/...` paths.
3. Sound events appear in logs during tone/creature playback.
4. If SD is removed/corrupted, boot still completes with fallback indicator and face.
